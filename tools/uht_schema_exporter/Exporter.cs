using System.Reflection;
using System.Text.Json;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace CyUEDiff.UhtExporter;

// Runs only as an explicitly selected, host-built UHT exporter. Core never loads it.
[UnrealHeaderTool]
public static class Exporter
{
    private static readonly string[] Facts = {
        "ClassType", "ClassFlags", "ScriptStructFlags", "ScriptStructExportFlags",
        "PropertyFlags", "ArrayDimensions", "ScriptStruct", "Enum", "UnderlyingType",
        "Class", "MetaClass", "Super", "SuperClass", "CppType"
    };

    private static object? Fact(object value)
    {
        if (value is UhtType type) return type.SourceName;
        if (value is string text) return text;
        if (value is Enum) return value.ToString();
        throw new InvalidOperationException("Unsupported UHT fact type: " + value.GetType().FullName);
    }

    private static Dictionary<string, object?> Node(UhtType type, int depth = 0)
    {
        if (depth > 64) throw new InvalidOperationException("UHT metadata nesting exceeds the export limit");
        var result = new Dictionary<string, object?> {
            ["EngineClassName"] = type.EngineClassName,
            ["EngineName"] = type.EngineName,
            ["SourceName"] = type.SourceName
        };
        foreach (var key in Facts)
        {
            var property = type.GetType().GetProperty(key, BindingFlags.Public | BindingFlags.Instance);
            if (property == null || property.GetIndexParameters().Length != 0) continue;
            var value = property.GetValue(type);
            if (value != null) result[key] = Fact(value);
        }
        // Use the actual derived inner property, retaining struct/enum references.
        foreach (var key in new[] { "KeyProperty", "ValueProperty" })
        {
            var value = type.GetType().GetProperty(key)?.GetValue(type);
            if (value is UhtProperty property) result[key] = Node(property, depth + 1);
        }
        if (type is UhtEnum enumeration)
            result["EnumValues"] = enumeration.EnumValues.Select(value => new {value.Name, value.Value}).ToArray();
        result["Children"] = type.Children.Select(child => Node(child, depth + 1)).ToArray();
        return result;
    }

    [UhtExporter(Name = "CyUEDiffSchemas", Description = "Export complete field type references for standalone inspection", Options = UhtExporterOptions.Default, ModuleName = "CyUEDiffSchemas")]
    public static void Export(IUhtExportFactory factory)
    {
        foreach (var package in factory.Session.Packages)
        {
            var result = Node(package);
            result["schema"] = "cyuediff.uht/1";
            factory.CommitOutput(factory.MakePath(package, ".cyuediff.json"), JsonSerializer.Serialize(result));
        }
    }
}
