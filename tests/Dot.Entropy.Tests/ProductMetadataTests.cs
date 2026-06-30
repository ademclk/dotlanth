using Dot.Entropy.Api;

namespace Dot.Entropy.Tests;

public sealed class ProductMetadataTests
{
    [Fact]
    public void ProductMetadataMatchesEntropyScaffold()
    {
        Assert.Equal("dot-entropy", ProductMetadata.Product);
        Assert.Equal("Dot.Entropy.Api", ProductMetadata.ServiceName);
        Assert.Equal("v26.1.0", ProductMetadata.Version);
        Assert.Equal("/products/entropy", ProductMetadata.Route);
    }
}

