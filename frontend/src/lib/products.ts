import type { Icon } from "@phosphor-icons/react"
import {
  Aperture,
  Atom,
  Brain,
  Circuitry,
  Compass,
  Fingerprint,
  Flask,
  Graph,
  Hammer,
  Keyhole,
  ShieldCheck,
  Sparkle,
} from "@phosphor-icons/react"

import { apiEndpoints } from "./api"
import type { ApiEndpoint } from "./api"

export type ProductStatus = "online" | "scaffolded" | "planned"

export type ProductArea = {
  id: string
  name: string
  shortName: string
  route: string
  description: string
  version: string
  status: ProductStatus
  api?: ApiEndpoint
  icon: Icon
}

export const productAreas: ProductArea[] = [
  {
    id: "core",
    name: "dot-core",
    shortName: "Core",
    route: "/products/core",
    description: "Enterprise readiness, posture, and operating-system-grade product coordination.",
    version: "v26.1.0",
    status: "scaffolded",
    api: apiEndpoints.core,
    icon: ShieldCheck,
  },
  {
    id: "forge",
    name: "dot-forge",
    shortName: "Forge",
    route: "/products/forge",
    description: "PQC testbench surfaces for experiments, evidence, and migration planning.",
    version: "v26.1.0",
    status: "scaffolded",
    api: apiEndpoints.forge,
    icon: Hammer,
  },
  {
    id: "entropy",
    name: "dot-entropy",
    shortName: "Entropy",
    route: "/products/entropy",
    description: "Randomness, attestation, and signal-quality surfaces for future product work.",
    version: "v26.1.0",
    status: "scaffolded",
    api: apiEndpoints.entropy,
    icon: Aperture,
  },
  {
    id: "lab",
    name: "dot-lab",
    shortName: "Lab",
    route: "/products/lab",
    description: "Shared experiment workspace reserved for later product modules.",
    version: "future",
    status: "planned",
    icon: Flask,
  },
  {
    id: "trace",
    name: "dot-trace",
    shortName: "Trace",
    route: "/products/trace",
    description: "Evidence trails, audit lanes, and system history views.",
    version: "future",
    status: "planned",
    icon: Graph,
  },
  {
    id: "arena",
    name: "dot-arena",
    shortName: "Arena",
    route: "/products/arena",
    description: "Scenario spaces for evaluating competing plans and product states.",
    version: "future",
    status: "planned",
    icon: Atom,
  },
  {
    id: "guide",
    name: "dot-guide",
    shortName: "Guide",
    route: "/products/guide",
    description: "Operator guidance, checklists, and assisted navigation.",
    version: "future",
    status: "planned",
    icon: Compass,
  },
  {
    id: "foundry",
    name: "dot-foundry",
    shortName: "Foundry",
    route: "/products/foundry",
    description: "Reusable creation workflows for product and artifact assembly.",
    version: "future",
    status: "planned",
    icon: Circuitry,
  },
  {
    id: "planner",
    name: "dot-planner",
    shortName: "Planner",
    route: "/products/planner",
    description: "Planning lanes for milestones, release shape, and cross-product sequencing.",
    version: "future",
    status: "planned",
    icon: Brain,
  },
  {
    id: "sensorium",
    name: "dot-sensorium",
    shortName: "Sensorium",
    route: "/products/sensorium",
    description: "Signal dashboards and observability-adjacent product surfaces.",
    version: "future",
    status: "planned",
    icon: Sparkle,
  },
  {
    id: "workshop",
    name: "dot-workshop",
    shortName: "Workshop",
    route: "/products/workshop",
    description: "Hands-on build surfaces for repeatable product setup work.",
    version: "future",
    status: "planned",
    icon: Keyhole,
  },
  {
    id: "passport",
    name: "dot-passport",
    shortName: "Passport",
    route: "/products/passport",
    description: "Identity, access, and product boundary placeholders.",
    version: "future",
    status: "planned",
    icon: Fingerprint,
  },
]

export const apiBackedProducts = productAreas.filter((product) => product.api)

export function getProductArea(id: string) {
  return productAreas.find((product) => product.id === id)
}
