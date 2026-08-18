using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using AppleGlassCalculator.Helpers;
using AppleGlassCalculator.Models;
using AppleGlassCalculator.Services;

namespace AppleGlassCalculator;

public partial class MainWindow : Window
{
    private readonly CalculatorService _calculator = new();
    private readonly HistoryService _historyService = new();

    public ObservableCollection<HistoryItem> HistoryItems { get; } = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;

        foreach (var item in _historyService.Load().OrderByDescending(item => item.CreatedAt))
        {
            HistoryItems.Add(item);
        }

        UpdateDisplays();
        Loaded += (_, _) =>
        {
            Focus();
            UpdateWindowGeometry();
        };
        SizeChanged += (_, _) => UpdateWindowGeometry();
    }

    private void CalculatorButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string action })
        {
            HandleAction(action);
        }
    }

    private void HandleAction(string action)
    {
        if (action.Length == 1 && char.IsDigit(action[0]))
        {
            _calculator.InputDigit(action[0]);
        }
        else
        {
            switch (action)
            {
                case ".":
                    _calculator.InputDecimalPoint();
                    break;
                case "+":
                case "-":
                case "*":
                case "/":
                    _calculator.SetOperator(action);
                    break;
                case "=":
                    AddHistory(_calculator.Evaluate());
                    break;
                case "Back":
                    _calculator.Backspace();
                    break;
                case "CE":
                    _calculator.ClearEntry();
                    break;
                case "C":
                    _calculator.ClearAll();
                    break;
            }
        }

        UpdateDisplays();
    }

    private void AddHistory(CalculationResult? calculation)
    {
        if (calculation is null) return;

        HistoryItems.Insert(0, new HistoryItem
        {
            Expression = calculation.Expression,
            Result = calculation.Result
        });

        while (HistoryItems.Count > 50)
        {
            HistoryItems.RemoveAt(HistoryItems.Count - 1);
        }

        _historyService.Save(HistoryItems);
        HistoryScrollViewer.ScrollToLeftEnd();
    }

    private void UpdateDisplays()
    {
        ResultDisplay.Text = _calculator.DisplayValue;
        ExpressionDisplay.Text = _calculator.ExpressionText;
        HistoryEmptyText.Visibility = HistoryItems.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        HistoryScrollViewer.Visibility = HistoryItems.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
    }

    private void ClearHistoryButton_Click(object sender, RoutedEventArgs e)
    {
        HistoryItems.Clear();
        _historyService.Save(HistoryItems);
        UpdateDisplays();
    }

    private void HistoryScrollViewer_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        HistoryScrollViewer.ScrollToHorizontalOffset(HistoryScrollViewer.HorizontalOffset - e.Delta);
        e.Handled = true;
    }

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        var action = e.Key switch
        {
            Key.D0 or Key.NumPad0 => "0",
            Key.D1 or Key.NumPad1 => "1",
            Key.D2 or Key.NumPad2 => "2",
            Key.D3 or Key.NumPad3 => "3",
            Key.D4 or Key.NumPad4 => "4",
            Key.D5 or Key.NumPad5 => "5",
            Key.D6 or Key.NumPad6 => "6",
            Key.D7 or Key.NumPad7 => "7",
            Key.D8 or Key.NumPad8 => "8",
            Key.D9 or Key.NumPad9 => "9",
            Key.Add or Key.OemPlus => "+",
            Key.Subtract or Key.OemMinus => "-",
            Key.Multiply => "*",
            Key.Divide or Key.OemQuestion => "/",
            Key.Decimal or Key.OemPeriod => ".",
            Key.Enter or Key.Return => "=",
            Key.Back => "Back",
            Key.Delete => "CE",
            Key.Escape => "C",
            _ => null
        };

        if (action is null) return;
        HandleAction(action);
        e.Handled = true;
    }

    private void TitleArea_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2)
        {
            WindowHelper.ToggleMaximize(this);
            return;
        }

        BeginWindowDrag(e);
    }

    private void BeginWindowDrag(MouseButtonEventArgs e)
    {

        if (WindowState == WindowState.Maximized)
        {
            var cursorPosition = e.GetPosition(this);
            var horizontalRatio = cursorPosition.X / Math.Max(ActualWidth, 1);
            WindowState = WindowState.Normal;
            Left = SystemParameters.WorkArea.Left + cursorPosition.X - (Width * horizontalRatio);
            Top = Math.Max(SystemParameters.WorkArea.Top, cursorPosition.Y - 20);
        }

        WindowHelper.BeginDrag(this);
        e.Handled = true;
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e) => Close();
    private void MinimizeButton_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void DragButton_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        BeginWindowDrag(e);
    }

    private void Window_StateChanged(object? sender, EventArgs e)
    {
        UpdateWindowGeometry();
    }

    private void UpdateWindowGeometry()
    {
        var radius = WindowState == WindowState.Maximized ? 0d : 38d;
        WindowShell.CornerRadius = new CornerRadius(radius);
        WindowShell.Margin = new Thickness(0);

        if (WindowShell.ActualWidth > 0 && WindowShell.ActualHeight > 0)
        {
            WindowShell.Clip = new RectangleGeometry(
                new Rect(0, 0, WindowShell.ActualWidth, WindowShell.ActualHeight),
                radius,
                radius);
        }

    }

    protected override void OnClosed(EventArgs e)
    {
        _historyService.Save(HistoryItems);
        base.OnClosed(e);
    }
}
