var builder = DistributedApplication.CreateBuilder(args);

var postgres = builder.AddPostgres("postgres");
var valkey = builder.AddValkey("valkey");
var frontendPath = Path.GetFullPath(Path.Combine(builder.AppHostDirectory, "../../frontend"));

var coreApi = builder.AddProject<Projects.Dot_Core_Api>("dot-core-api")
    .WithReference(postgres)
    .WithReference(valkey)
    .WaitFor(postgres)
    .WaitFor(valkey);

var forgeApi = builder.AddProject<Projects.Dot_Forge_Api>("dot-forge-api")
    .WithReference(postgres)
    .WithReference(valkey)
    .WaitFor(postgres)
    .WaitFor(valkey);

var entropyApi = builder.AddProject<Projects.Dot_Entropy_Api>("dot-entropy-api")
    .WithReference(postgres)
    .WithReference(valkey)
    .WaitFor(postgres)
    .WaitFor(valkey);

builder.AddExecutable("frontend", "npm", frontendPath, "run", "dev", "--", "--host", "localhost", "--port", "3000")
    .WithHttpEndpoint(targetPort: 3000, port: 3000, name: "http", isProxied: false)
    .WithExternalHttpEndpoints()
    .WithEnvironment("VITE_DOT_CORE_API_BASE_URL", coreApi.GetEndpoint("http"))
    .WithEnvironment("VITE_DOT_FORGE_API_BASE_URL", forgeApi.GetEndpoint("http"))
    .WithEnvironment("VITE_DOT_ENTROPY_API_BASE_URL", entropyApi.GetEndpoint("http"))
    .WaitFor(coreApi)
    .WaitFor(forgeApi)
    .WaitFor(entropyApi);

builder.Build().Run();
