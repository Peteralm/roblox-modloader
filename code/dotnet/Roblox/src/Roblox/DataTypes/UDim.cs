using System.Runtime.InteropServices;

namespace Roblox;

[StructLayout(LayoutKind.Sequential)]
public readonly struct UDim(float scale, float offset) : IEquatable<UDim>, IRobloxDataType
{
    public float Scale { get; } = scale;
    public float Offset { get; } = offset;

    public static readonly UDim Zero = new(0f, 0);

    public static UDim operator +(UDim a, UDim b) => new(a.Scale + b.Scale, a.Offset + b.Offset);
    public static UDim operator -(UDim a, UDim b) => new(a.Scale - b.Scale, a.Offset - b.Offset);

    public bool Equals(UDim other) => Scale.Equals(other.Scale) && Offset.Equals(other.Offset);
    public override bool Equals(object? obj) => obj is UDim other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Scale, Offset);

    public static bool operator ==(UDim a, UDim b) => a.Equals(b);
    public static bool operator !=(UDim a, UDim b) => !a.Equals(b);

    public override string ToString() => $"{Scale}, {Offset}";
}
