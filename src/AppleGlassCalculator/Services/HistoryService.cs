using System.IO;
using System.Text.Json;
using AppleGlassCalculator.Models;

namespace AppleGlassCalculator.Services;

public sealed class HistoryService
{
    private const int MaximumItems = 50;
    private readonly string _historyPath;

    public HistoryService()
    {
        var dataDirectory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AppleGlassCalculator");
        Directory.CreateDirectory(dataDirectory);
        _historyPath = Path.Combine(dataDirectory, "history.json");
    }

    public IReadOnlyList<HistoryItem> Load()
    {
        try
        {
            if (!File.Exists(_historyPath))
            {
                return Array.Empty<HistoryItem>();
            }

            var json = File.ReadAllText(_historyPath);
            return JsonSerializer.Deserialize<List<HistoryItem>>(json)
                   ?? new List<HistoryItem>();
        }
        catch
        {
            return Array.Empty<HistoryItem>();
        }
    }

    public void Save(IEnumerable<HistoryItem> items)
    {
        try
        {
            var snapshot = items.Take(MaximumItems).ToList();
            var tempPath = _historyPath + ".tmp";
            File.WriteAllText(tempPath, JsonSerializer.Serialize(snapshot));
            File.Move(tempPath, _historyPath, true);
        }
        catch
        {
            // History must never prevent the calculator itself from working.
        }
    }
}
