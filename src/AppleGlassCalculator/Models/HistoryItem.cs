namespace AppleGlassCalculator.Models;

public sealed class HistoryItem
{
    public Guid Id { get; set; } = Guid.NewGuid();
    public string Expression { get; set; } = string.Empty;
    public string Result { get; set; } = string.Empty;
    public DateTimeOffset CreatedAt { get; set; } = DateTimeOffset.Now;
}
