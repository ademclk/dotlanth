import { describe, expect, it } from "vitest"

import { apiEndpoints, getApiStatusUrl } from "./api"
import { apiBackedProducts, getProductArea, productAreas } from "./products"

describe("product route registry", () => {
  it("keeps the initial API-backed product routes stable", () => {
    expect(apiBackedProducts.map((product) => product.route)).toEqual([
      "/products/core",
      "/products/forge",
      "/products/entropy",
    ])
  })

  it("reserves the wider shared frontend route set", () => {
    expect(productAreas).toHaveLength(12)
    expect(getProductArea("passport")?.route).toBe("/products/passport")
  })
})

describe("api endpoint configuration", () => {
  it("maps Vite environment names to product APIs", () => {
    expect(apiEndpoints.core.envName).toBe("VITE_DOT_CORE_API_BASE_URL")
    expect(apiEndpoints.forge.envName).toBe("VITE_DOT_FORGE_API_BASE_URL")
    expect(apiEndpoints.entropy.envName).toBe("VITE_DOT_ENTROPY_API_BASE_URL")
  })

  it("leaves status URLs unset until AppHost or a local env file provides base URLs", () => {
    expect(getApiStatusUrl("core")).toBeUndefined()
  })
})
