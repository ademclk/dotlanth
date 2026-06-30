using Dot.Core.Api;

namespace Dot.Core.Tests;

public sealed class ProductMetadataTests
{
    [Fact]
    public void ProductMetadataMatchesCoreScaffold()
    {
        Assert.Equal("dot-core", ProductMetadata.Product);
        Assert.Equal("Dot.Core.Api", ProductMetadata.ServiceName);
        Assert.Equal("v26.1.0", ProductMetadata.Version);
        Assert.Equal("/products/core", ProductMetadata.Route);
    }
}

