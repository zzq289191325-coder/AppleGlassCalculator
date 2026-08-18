using System.Globalization;

namespace AppleGlassCalculator.Services;

public sealed record CalculationResult(string Expression, string Result);

public sealed class CalculatorService
{
    private const int MaximumInputLength = 29;
    private string _input = "0";
    private decimal? _accumulator;
    private string? _pendingOperator;
    private string? _lastOperator;
    private decimal? _lastOperand;
    private bool _startNewInput = true;
    private bool _justEvaluated;
    private string _expression = string.Empty;

    public string DisplayValue { get; private set; } = "0";
    public string ExpressionText => _expression;
    public bool HasError { get; private set; }

    public void InputDigit(char digit)
    {
        if (!char.IsDigit(digit)) return;
        RecoverFromError();

        if (_justEvaluated)
        {
            ResetForFreshInput();
        }

        if (_startNewInput)
        {
            _input = digit.ToString();
            _startNewInput = false;
        }
        else if (CountDigits(_input) < MaximumInputLength)
        {
            _input = _input == "0" ? digit.ToString() : _input + digit;
        }

        DisplayValue = FormatInput(_input);
    }

    public void InputDecimalPoint()
    {
        RecoverFromError();

        if (_justEvaluated)
        {
            ResetForFreshInput();
        }

        if (_startNewInput)
        {
            _input = "0.";
            _startNewInput = false;
        }
        else if (!_input.Contains('.'))
        {
            _input += ".";
        }

        DisplayValue = FormatInput(_input);
    }

    public void SetOperator(string operation)
    {
        if (!IsOperator(operation)) return;
        RecoverFromError();

        if (!TryReadInput(out var current))
        {
            ShowError("输入无效");
            return;
        }

        if (_pendingOperator is not null && !_startNewInput)
        {
            if (!TryCalculate(_accumulator ?? current, current, _pendingOperator, out var chained))
            {
                return;
            }

            _accumulator = chained;
            _input = ToRawString(chained);
            DisplayValue = FormatNumber(chained);
        }
        else if (_accumulator is null || _justEvaluated)
        {
            _accumulator = current;
        }

        _pendingOperator = operation;
        _expression = $"{FormatNumber(_accumulator.Value)} {ToSymbol(operation)}";
        _startNewInput = true;
        _justEvaluated = false;
    }

    public CalculationResult? Evaluate()
    {
        RecoverFromError();
        if (!TryReadInput(out var current))
        {
            ShowError("输入无效");
            return null;
        }

        string? operation;
        decimal left;
        decimal right;

        if (_pendingOperator is not null)
        {
            operation = _pendingOperator;
            left = _accumulator ?? current;
            right = current;
            _lastOperator = operation;
            _lastOperand = right;
        }
        else if (_justEvaluated && _lastOperator is not null && _lastOperand.HasValue)
        {
            operation = _lastOperator;
            left = current;
            right = _lastOperand.Value;
        }
        else
        {
            return null;
        }

        if (!TryCalculate(left, right, operation, out var result))
        {
            return null;
        }

        var expression = $"{FormatNumber(left)} {ToSymbol(operation)} {FormatNumber(right)}";
        _expression = expression + " =";
        _input = ToRawString(result);
        DisplayValue = FormatNumber(result);
        _accumulator = null;
        _pendingOperator = null;
        _startNewInput = true;
        _justEvaluated = true;
        return new CalculationResult(expression, DisplayValue);
    }

    public void Backspace()
    {
        if (HasError)
        {
            ClearEntry();
            return;
        }

        if (_startNewInput || _justEvaluated) return;

        _input = _input.Length > 1 ? _input[..^1] : "0";
        if (_input is "-" or "") _input = "0";
        DisplayValue = FormatInput(_input);
    }

    public void ClearEntry()
    {
        HasError = false;
        _input = "0";
        DisplayValue = "0";
        _startNewInput = true;
        _justEvaluated = false;
    }

    public void ClearAll()
    {
        _input = "0";
        _accumulator = null;
        _pendingOperator = null;
        _lastOperator = null;
        _lastOperand = null;
        _startNewInput = true;
        _justEvaluated = false;
        _expression = string.Empty;
        DisplayValue = "0";
        HasError = false;
    }

    private bool TryCalculate(decimal left, decimal right, string operation, out decimal result)
    {
        result = 0;
        if (operation == "/" && right == 0)
        {
            ShowError("无法除以零");
            return false;
        }

        try
        {
            result = operation switch
            {
                "+" => left + right,
                "-" => left - right,
                "*" => left * right,
                "/" => left / right,
                _ => throw new InvalidOperationException("Unsupported operation")
            };
            return true;
        }
        catch (OverflowException)
        {
            ShowError("数值超出范围");
            return false;
        }
    }

    private void ShowError(string message)
    {
        DisplayValue = message;
        _expression = string.Empty;
        HasError = true;
        _accumulator = null;
        _pendingOperator = null;
        _startNewInput = true;
        _justEvaluated = false;
    }

    private void RecoverFromError()
    {
        if (HasError) ClearAll();
    }

    private void ResetForFreshInput()
    {
        _input = "0";
        _accumulator = null;
        _pendingOperator = null;
        _expression = string.Empty;
        _justEvaluated = false;
        _startNewInput = true;
    }

    private bool TryReadInput(out decimal value) =>
        decimal.TryParse(_input.TrimEnd('.'), NumberStyles.Number, CultureInfo.InvariantCulture, out value);

    private static bool IsOperator(string operation) => operation is "+" or "-" or "*" or "/";
    private static int CountDigits(string value) => value.Count(char.IsDigit);
    private static string ToRawString(decimal value) => value.ToString(CultureInfo.InvariantCulture);
    private static string ToSymbol(string operation) => operation switch
    {
        "*" => "×",
        "/" => "÷",
        _ => operation
    };

    public static string FormatNumber(decimal value) =>
        value.ToString("#,0.############################", CultureInfo.InvariantCulture);

    private static string FormatInput(string raw)
    {
        var negative = raw.StartsWith('-');
        var unsigned = negative ? raw[1..] : raw;
        var parts = unsigned.Split('.', 2);

        if (!decimal.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out var integer))
        {
            return raw;
        }

        var formatted = integer.ToString("#,0", CultureInfo.InvariantCulture);
        if (parts.Length == 2)
        {
            formatted += "." + parts[1];
        }

        return negative ? "-" + formatted : formatted;
    }
}
