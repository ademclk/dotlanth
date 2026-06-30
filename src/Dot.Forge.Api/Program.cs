using Dot.Forge.Api;

var builder = WebApplication.CreateBuilder(args);

builder.AddServiceDefaults();
builder.Services.AddOpenApi();

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
}

app.MapDefaultEndpoints();
app.MapGet("/", () => Results.Redirect("/status")).ExcludeFromDescription();
app.MapGet("/status", (IConfiguration configuration, IHostEnvironment environment) =>
{
    return Results.Ok(new ProductStatus(
        ProductMetadata.Product,
        ProductMetadata.ServiceName,
        ProductMetadata.Version,
        ProductMetadata.Route,
        environment.EnvironmentName,
        HasConnectionString(configuration, "postgres"),
        HasConnectionString(configuration, "valkey")));
})
.WithName("GetForgeStatus");

app.Run();

static bool HasConnectionString(IConfiguration configuration, string name)
{
    return !string.IsNullOrWhiteSpace(configuration.GetConnectionString(name));
}

public sealed record ProductStatus(
    string Product,
    string Service,
    string Version,
    string FrontendRoute,
    string Environment,
    bool HasPostgresConnection,
    bool HasValkeyConnection);

