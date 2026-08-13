# Ble(e)p website

The static marketing site for [bleep.hml.tech](https://bleep.hml.tech), hosted
with Firebase Hosting in the `hml-studio` Google Cloud project.

## Preview locally

```sh
cd website
firebase serve --only hosting --project hml-studio
```

## Deploy

```sh
cd website
firebase deploy --only hosting --project hml-studio
```

The public directory is intentionally dependency-free. Firmware UI screenshots
and the owner's guide are copied into this directory; the generated product
photography, responsive source, and compatibility evidence ledger are committed
alongside them so the live site stays reproducible from this repository.
