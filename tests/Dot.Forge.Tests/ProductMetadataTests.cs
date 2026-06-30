using Dot.Forge.Api;

namespace Dot.Forge.Tests;

public sealed class ProductMetadataTests
{
    [Fact]
    public void ProductMetadataMatchesForgeScaffold()
    {
        Assert.Equal("dot-forge", ProductMetadata.Product);
        Assert.Equal("Dot.Forge.Api", ProductMetadata.ServiceName);
        Assert.Equal("v26.1.0", ProductMetadata.Version);
        Assert.Equal("/products/forge", ProductMetadata.Route);
    }
}

