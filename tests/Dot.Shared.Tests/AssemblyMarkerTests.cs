using Dot.Shared;

namespace Dot.Shared.Tests;

public sealed class AssemblyMarkerTests
{
    [Fact]
    public void AssemblyMarkerIdentifiesSharedAssembly()
    {
        Assert.Equal("Dot.Shared", typeof(AssemblyMarker).Assembly.GetName().Name);
    }
}

