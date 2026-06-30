type ProductApiKey = "core" | "forge" | "entropy"

export type ApiEndpoint = {
  key: ProductApiKey
  label: string
  envName: string
  baseUrl: string
  statusPath: "/status"
}

const env = import.meta.env

export const apiEndpoints: Record<ProductApiKey, ApiEndpoint> = {
  core: {
    key: "core",
    label: "Core API",
    envName: "VITE_DOT_CORE_API_BASE_URL",
    baseUrl: env.VITE_DOT_CORE_API_BASE_URL ?? "",
    statusPath: "/status",
  },
  forge: {
    key: "forge",
    label: "Forge API",
    envName: "VITE_DOT_FORGE_API_BASE_URL",
    baseUrl: env.VITE_DOT_FORGE_API_BASE_URL ?? "",
    statusPath: "/status",
  },
  entropy: {
    key: "entropy",
    label: "Entropy API",
    envName: "VITE_DOT_ENTROPY_API_BASE_URL",
    baseUrl: env.VITE_DOT_ENTROPY_API_BASE_URL ?? "",
    statusPath: "/status",
  },
}

export function getApiStatusUrl(key: ProductApiKey) {
  const endpoint = apiEndpoints[key]

  if (!endpoint.baseUrl) {
    return undefined
  }

  return new URL(endpoint.statusPath, endpoint.baseUrl).toString()
}
