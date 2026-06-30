import { createFileRoute } from "@tanstack/react-router"
import {
  ArrowRight,
  BracketsCurly,
  Database,
  GitBranch,
  SquaresFour,
} from "@phosphor-icons/react"

import { Button } from "@/components/ui/button"
import { apiBackedProducts, productAreas } from "@/lib/products"

export const Route = createFileRoute("/")({ component: HomeRoute })

function HomeRoute() {
  return (
    <main className="min-h-svh bg-background text-foreground">
      <section className="mx-auto grid w-full max-w-7xl gap-8 px-5 py-6 sm:px-8 lg:grid-cols-[280px_1fr] lg:px-10">
        <aside className="lg:sticky lg:top-6 lg:h-[calc(100svh-3rem)]">
          <div className="flex h-full flex-col justify-between border-border/80 border-r pr-6">
            <div className="space-y-8">
              <a className="flex items-center gap-3" href="/">
                <span className="grid size-9 place-items-center rounded-md bg-primary text-primary-foreground">
                  <SquaresFour className="size-5" weight="fill" />
                </span>
                <span>
                  <span className="block font-heading text-xl">dotlanth</span>
                  <span className="block text-muted-foreground text-xs uppercase tracking-[0.16em]">
                    v26.1.0 scaffold
                  </span>
                </span>
              </a>

              <nav aria-label="Products" className="grid gap-1">
                {productAreas.map((product) => {
                  const Icon = product.icon

                  return (
                    <a
                      className="group flex items-center justify-between rounded-md px-3 py-2 text-sm transition hover:bg-accent hover:text-accent-foreground"
                      href={product.route}
                      key={product.id}
                    >
                      <span className="flex items-center gap-2">
                        <Icon className="size-4 text-primary" />
                        {product.shortName}
                      </span>
                      <span className="text-muted-foreground text-xs group-hover:text-accent-foreground">
                        {product.version}
                      </span>
                    </a>
                  )
                })}
              </nav>
            </div>

            <div className="hidden gap-2 text-muted-foreground text-xs leading-relaxed lg:grid">
              <span>AppHost controls APIs, Postgres, Valkey, and this frontend.</span>
              <span>Product folders stay versioned until submodule remotes exist.</span>
            </div>
          </div>
        </aside>

        <div className="space-y-8">
          <header className="grid gap-6 border-border/70 border-b pb-8 lg:grid-cols-[1fr_320px]">
            <div className="max-w-3xl space-y-5">
              <div className="inline-flex items-center gap-2 rounded-md border border-border px-3 py-1 text-muted-foreground text-xs uppercase tracking-[0.16em]">
                <GitBranch className="size-4" />
                parent repo coordination
              </div>
              <div className="space-y-4">
                <h1 className="font-heading text-4xl leading-tight sm:text-5xl">
                  Shared runtime, one shell, many product lanes.
                </h1>
                <p className="max-w-2xl text-muted-foreground text-sm leading-7 sm:text-base">
                  dotlanth starts product development from a single operational surface:
                  Aspire orchestration, product API placeholders, shared contracts, and
                  one shadcn frontend that routes across the dot product family.
                </p>
              </div>
              <div className="flex flex-wrap gap-3">
                <Button asChild>
                  <a href="/products/core">
                    Open Core
                    <ArrowRight className="size-4" />
                  </a>
                </Button>
                <Button asChild variant="outline">
                  <a href="/products/forge">Open Forge</a>
                </Button>
                <Button asChild variant="secondary">
                  <a href="/products/entropy">Open Entropy</a>
                </Button>
              </div>
            </div>

            <div className="grid content-start gap-3 rounded-md border border-border bg-card p-4">
              <div className="flex items-center gap-2 text-sm">
                <Database className="size-4 text-primary" />
                Local resources
              </div>
              {["postgres", "valkey"].map((resource) => (
                <div
                  className="flex items-center justify-between rounded-md bg-muted px-3 py-2 text-sm"
                  key={resource}
                >
                  <span>{resource}</span>
                  <span className="text-muted-foreground text-xs">Aspire-managed</span>
                </div>
              ))}
            </div>
          </header>

          <section className="grid gap-4 md:grid-cols-3">
            {apiBackedProducts.map((product) => {
              const Icon = product.icon

              return (
                <a
                  className="group rounded-md border border-border bg-card p-5 transition hover:-translate-y-0.5 hover:border-primary/60"
                  href={product.route}
                  key={product.id}
                >
                  <div className="mb-8 flex items-start justify-between">
                    <span className="grid size-10 place-items-center rounded-md bg-primary/10 text-primary">
                      <Icon className="size-5" />
                    </span>
                    <span className="rounded-md bg-muted px-2 py-1 text-muted-foreground text-xs">
                      {product.version}
                    </span>
                  </div>
                  <h2 className="font-heading text-2xl">{product.name}</h2>
                  <p className="mt-3 min-h-20 text-muted-foreground text-sm leading-6">
                    {product.description}
                  </p>
                  <div className="mt-5 flex items-center justify-between text-sm">
                    <span className="text-primary">{product.api?.label}</span>
                    <ArrowRight className="size-4 transition group-hover:translate-x-1" />
                  </div>
                </a>
              )
            })}
          </section>

          <section className="rounded-md border border-border">
            <div className="grid gap-4 border-border border-b p-5 sm:grid-cols-[1fr_auto] sm:items-center">
              <div>
                <h2 className="font-heading text-2xl">Route Registry</h2>
                <p className="mt-1 text-muted-foreground text-sm">
                  Shared navigation reserves product space without creating separate apps.
                </p>
              </div>
              <div className="inline-flex items-center gap-2 rounded-md bg-muted px-3 py-2 text-sm">
                <BracketsCurly className="size-4 text-primary" />
                {productAreas.length} routes
              </div>
            </div>
            <div className="grid divide-y divide-border">
              {productAreas.map((product) => (
                <a
                  className="grid gap-3 px-5 py-4 text-sm transition hover:bg-accent sm:grid-cols-[150px_1fr_110px]"
                  href={product.route}
                  key={product.id}
                >
                  <span className="font-medium">{product.name}</span>
                  <span className="text-muted-foreground">{product.route}</span>
                  <span className="text-muted-foreground sm:text-right">{product.status}</span>
                </a>
              ))}
            </div>
          </section>
        </div>
      </section>
    </main>
  )
}
