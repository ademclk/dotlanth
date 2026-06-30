import { createFileRoute } from "@tanstack/react-router"
import { ArrowLeft, CheckCircle, Cloud, Database, Plugs } from "@phosphor-icons/react"

import { Button } from "@/components/ui/button"
import { getApiStatusUrl } from "@/lib/api"
import { getProductArea } from "@/lib/products"

export const Route = createFileRoute("/products/$productId")({
  component: ProductRoute,
})

function ProductRoute() {
  const { productId } = Route.useParams()
  const product = getProductArea(productId)

  if (!product) {
    return (
      <main className="grid min-h-svh place-items-center bg-background px-6 text-foreground">
        <div className="max-w-md rounded-md border border-border bg-card p-6">
          <p className="text-muted-foreground text-sm">Unknown product route.</p>
          <Button asChild className="mt-5">
            <a href="/">Return Home</a>
          </Button>
        </div>
      </main>
    )
  }

  const Icon = product.icon
  const statusUrl = product.api ? getApiStatusUrl(product.api.key) : undefined

  return (
    <main className="min-h-svh bg-background text-foreground">
      <section className="mx-auto grid w-full max-w-6xl gap-8 px-5 py-6 sm:px-8 lg:px-10">
        <header className="grid gap-8 border-border/70 border-b pb-8 lg:grid-cols-[1fr_320px]">
          <div className="space-y-6">
            <Button asChild size="sm" variant="ghost">
              <a href="/">
                <ArrowLeft className="size-4" />
                dotlanth
              </a>
            </Button>
            <div className="space-y-4">
              <div className="flex items-center gap-3">
                <span className="grid size-12 place-items-center rounded-md bg-primary/10 text-primary">
                  <Icon className="size-6" />
                </span>
                <span className="rounded-md bg-muted px-2 py-1 text-muted-foreground text-xs uppercase tracking-[0.14em]">
                  {product.status}
                </span>
              </div>
              <h1 className="font-heading text-4xl leading-tight sm:text-5xl">{product.name}</h1>
              <p className="max-w-2xl text-muted-foreground text-sm leading-7 sm:text-base">
                {product.description}
              </p>
            </div>
          </div>

          <div className="grid content-start gap-3 rounded-md border border-border bg-card p-4">
            <div className="flex items-center gap-2 text-sm">
              <Cloud className="size-4 text-primary" />
              Runtime Link
            </div>
            <RuntimeRow label="Version" value={product.version} />
            <RuntimeRow label="Route" value={product.route} />
            <RuntimeRow label="API" value={product.api?.label ?? "not activated"} />
          </div>
        </header>

        <section className="grid gap-4 md:grid-cols-3">
          <RuntimePanel
            icon={<Plugs className="size-5" />}
            label="Endpoint"
            value={statusUrl ?? "configured when API exists"}
          />
          <RuntimePanel
            icon={<Database className="size-5" />}
            label="PostgreSQL"
            value={product.api ? "ConnectionStrings__postgres" : "reserved"}
          />
          <RuntimePanel
            icon={<CheckCircle className="size-5" />}
            label="Valkey"
            value={product.api ? "ConnectionStrings__valkey" : "reserved"}
          />
        </section>
      </section>
    </main>
  )
}

function RuntimeRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-center justify-between gap-3 rounded-md bg-muted px-3 py-2 text-sm">
      <span className="text-muted-foreground">{label}</span>
      <span className="truncate font-medium">{value}</span>
    </div>
  )
}

function RuntimePanel({
  icon,
  label,
  value,
}: {
  icon: React.ReactNode
  label: string
  value: string
}) {
  return (
    <div className="rounded-md border border-border bg-card p-5">
      <span className="grid size-10 place-items-center rounded-md bg-primary/10 text-primary">
        {icon}
      </span>
      <h2 className="mt-6 font-heading text-xl">{label}</h2>
      <p className="mt-2 break-words text-muted-foreground text-sm leading-6">{value}</p>
    </div>
  )
}
