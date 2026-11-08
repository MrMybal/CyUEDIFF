"""Export host reflection metadata through matching UE 5.3 UHT, without Editor.

All generated manifests, compiler/cache outputs and reports stay in an ignored
workspace build directory. Original project/engine headers are fingerprinted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def export_metadata(engine_root, manifest_path, project, xml_cache, output, dotnet=None):
    workspace = Path(__file__).resolve().parents[1]
    output = output.resolve()
    if not output.is_relative_to(workspace) or not output.relative_to(workspace).parts[0].startswith('build'):
        raise ValueError('Outputs must stay in an ignored workspace build directory')
    engine_root = engine_root.resolve(strict=True)
    engine = engine_root if (engine_root/'Build/Build.version').is_file() else engine_root/'Engine'
    version_file = engine/'Build/Build.version'
    version = json.loads(version_file.read_text(encoding='utf-8'))
    if (version['MajorVersion'], version['MinorVersion']) != (5, 3):
        raise ValueError('This exporter driver is validated for UE 5.3; other host APIs need validation')
    manifest_path, project, xml_cache = [p.resolve(strict=True) for p in (manifest_path, project, xml_cache)]
    for source in (engine, manifest_path, project, xml_cache):
        if source == output or source.is_relative_to(output) or output.is_relative_to(source):
            raise ValueError('Output directory overlaps a source')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('UhtPlugins'):
        raise ValueError('Use a manifest without external UHT plugins')
    if Path(manifest['RootLocalPath']).resolve(strict=True) != engine.parent.resolve():
        raise ValueError('Selected engine root disagrees with the original UHT manifest')
    modules = manifest['Modules']
    if not modules or len(modules) > 4096:
        raise ValueError('Invalid module count')
    module_names = {m['Name'] for m in modules}
    if len(module_names) != len(modules) or 'CyUEDiffSchemas' in module_names:
        raise ValueError('Duplicate or reserved exporter module name')
    if any(not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name) for name in module_names):
        raise ValueError('Unsafe module name')
    header_keys = ('ClassesHeaders', 'PublicHeaders', 'InternalHeaders', 'PrivateHeaders')
    headers = sorted({Path(value).resolve(strict=True) for module in modules for key in header_keys for value in module.get(key, [])})
    if len(headers) > 65536 or sum(p.stat().st_size for p in headers) > 512*1024*1024:
        raise ValueError('Header corpus limit exceeded')
    originals = {str(p):digest(p) for p in {manifest_path, project, xml_cache, version_file, *headers}}
    assembly_dir = engine/'Binaries/DotNET/UnrealBuildTool'
    tool = assembly_dir/'UnrealBuildTool.dll'
    runtime = json.loads((assembly_dir/'UnrealBuildTool.runtimeconfig.json').read_text(encoding='utf-8'))
    framework = runtime['runtimeOptions']['tfm']
    if framework != 'net6.0':
        raise ValueError('Unexpected UHT runtime profile')
    if dotnet is None:
        candidates = sorted((engine/'Binaries/ThirdParty/DotNet').glob('6.*/windows/dotnet.exe'))
        if len(candidates) != 1:
            raise ValueError('Select the host dotnet executable explicitly')
        dotnet = candidates[0]
    dotnet = dotnet.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    env = {k.upper() if os.name == 'nt' else k:v for k,v in os.environ.items()}
    env.update({'DOTNET_CLI_HOME':str(output/'dotnet-home'), 'DOTNET_SKIP_FIRST_TIME_EXPERIENCE':'1',
                'DOTNET_CLI_TELEMETRY_OPTOUT':'1', 'DOTNET_GENERATE_ASPNET_CERTIFICATE':'false',
                'DOTNET_ADD_GLOBAL_TOOLS_TO_PATH':'false', 'DOTNET_NOLOGO':'1'})
    exporter = workspace/'tools/uht_schema_exporter'
    for path in [tool, assembly_dir/'EpicGames.UHT.dll', assembly_dir/'EpicGames.Core.dll', *exporter.iterdir()]:
        if path.is_file(): originals[str(path)] = digest(path)
    command = [str(dotnet), 'build', str(exporter/'CyUEDiff.UhtExporter.csproj'), '--configuration', 'Release',
               '--output', str(output/'exporter'), '--configfile', str(exporter/'NuGet.Config'),
               '-p:TargetFramework='+framework, '-p:UhtAssemblyDirectory='+str(assembly_dir),
               '-p:BaseIntermediateOutputPath='+str(output/'exporter-obj')+'/']
    result_code = -1
    try:
        build = subprocess.run(command, env=env, capture_output=True, timeout=120)
        (output/'exporter-build.log').write_bytes(build.stdout+build.stderr)
        if build.returncode:
            result_code = build.returncode
            raise RuntimeError('Exporter compilation failed; inspect exporter-build.log')
        library = output/'exporter/CyUEDiff.UhtExporter.dll'
        # An empty, isolated module gives the UHT plugin its required output owner.
        shim = dict(modules[-1])
        shim.update(Name='CyUEDiffSchemas', BaseDirectory=str(exporter), IncludeBase=str(exporter), SaveExportedHeaders=False)
        for key in (*header_keys, 'PublicDefines'): shim[key] = []
        modules.append(shim)
        for module in modules:
            directory = output/'modules'/module['Name']
            directory.mkdir(parents=True, exist_ok=True)
            module['OutputDirectory'] = str(directory)
            module['GeneratedCPPFilenameBase'] = str(directory/(module['Name']+'.gen'))
        manifest['ExternalDependenciesFile'] = str(output/'external-dependencies.txt')
        manifest['UhtPlugins'] = [str(library)]
        isolated = output/'isolated.uhtmanifest'
        isolated.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        cache = output/'XmlConfigCache.bin'
        shutil.copyfile(xml_cache, cache)
        command = [str(dotnet), str(tool), '-Mode=UnrealHeaderTool', str(project), str(isolated),
                   '-NoCodeGen', '-NoLog', '-XmlConfigCache='+str(cache)]
        run = subprocess.run(command, env=env, capture_output=True, timeout=240)
        (output/'run.log').write_bytes(run.stdout+run.stderr)
        result_code = run.returncode
        if run.returncode: raise RuntimeError('UHT metadata export failed; inspect run.log')
        outputs = list((output/'modules').rglob('*.cyuediff.json'))
        if {path.name for path in outputs} != {module['Name']+'.cyuediff.json' for module in modules} or len(outputs) != len(modules):
            raise RuntimeError('UHT did not produce every expected module description')
        for path in outputs:
            data = json.loads(path.read_text(encoding='utf-8'))
            if data.get('schema') != 'cyuediff.uht/1':
                raise RuntimeError('Unexpected exported metadata schema')
        return {'modules':len(modules), 'headers':len(headers), 'metadataFiles':len(outputs), 'engineBuild':version}
    finally:
        unchanged = all(digest(Path(path)) == value for path,value in originals.items())
        report = {'exitCode':result_code, 'originalsUnchanged':unchanged, 'sourceFingerprints':originals,
                  'engineBuild':version, 'metadataFiles':len(list((output/'modules').rglob('*.cyuediff.json')))}
        (output/'provenance.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if not unchanged: raise RuntimeError('An original input changed during metadata generation')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('engine-root', 'manifest', 'project', 'xml-cache'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=Path('build-native/uht-metadata'))
    parser.add_argument('--dotnet', type=Path)
    args = parser.parse_args()
    try:
        result = export_metadata(args.engine_root, args.manifest, args.project, args.xml_cache, args.output_dir, args.dotnet)
    except (ValueError, KeyError, OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(2, str(error)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
