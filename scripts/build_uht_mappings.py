"""Convert a complete host UHT metadata export to a local cooked USMAP."""
import argparse
import json
from pathlib import Path
from uht_mappings import build_cooked_mappings

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--metadata', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, default=Path('build-native/uht-mappings'))
args = parser.parse_args()
workspace = Path(__file__).resolve().parents[1]
metadata = args.metadata.resolve(strict=True)
out = args.output_dir.resolve()
if not metadata.is_dir(): parser.error('Expected a metadata directory')
if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
    parser.error('Generated mappings must stay in an ignored workspace build directory')
if out == metadata or out.is_relative_to(metadata) or metadata.is_relative_to(out):
    parser.error('Output directory overlaps metadata input')
out.mkdir(parents=True, exist_ok=True)
try:
    report = build_cooked_mappings(metadata, out/'native.usmap', out/'summary.json')
except (ValueError, KeyError, UnicodeError) as error:
    parser.exit(2, str(error)+'\n')
print(json.dumps({key:(len(value) if isinstance(value,(dict,list)) else value) for key,value in report.items()}, indent=2))
