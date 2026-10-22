# Download relay

Powers the page's optional **"Download it for me"** button. Browsers can't fetch `ultima4.zip` directly because its host sends no CORS header, so this tiny Azure Static Web App (Free plan, managed Functions) fetches that one fixed file server-side and returns it with a CORS header.

It is deliberately narrow (see `api/ultima4/index.js`):

- It only ever requests one hard-coded URL; the caller can't choose a URL.
- It only returns the bytes if their SHA-256 matches the pinned hash (and size). If the upstream file changes or is tampered with, it answers 502 and the page falls back to the manual "Choose ultima4.zip" flow.
- It adds CORS headers only for the allowed origins (the GitHub Pages site and a few localhost ports).
- It stores nothing. A warm instance keeps the verified bytes in memory.

If the upstream file legitimately changes, update `PINNED_SHA256` and `EXPECTED_BYTES` after checking the new file yourself.

## Deploying

One-time (Azure CLI, Free plan, same layout as the other projects):

```bash
az group create -n rg-ultima-assist -l westus2
az staticwebapp create -n ultima-assist-relay -g rg-ultima-assist -l westus2 --sku Free
# deployment token for the workflow:
az staticwebapp secrets list -n ultima-assist-relay -g rg-ultima-assist --query properties.apiKey -o tsv
```

Then in the GitHub repository:

1. Add the token as the Actions secret `AZURE_STATIC_WEB_APPS_API_TOKEN_RELAY`. `.github/workflows/relay.yml` deploys `relay/` whenever it changes.
2. Add the Actions variable `RELAY_URL` = `https://<default hostname of the app>/api/ultima4`. The Pages workflow bakes it into the page; without it the button stays hidden.

Check it: `curl -I -H "Origin: https://pataltimore.github.io" https://<hostname>/api/ultima4` should return `200`, `Content-Type: application/zip`, `Content-Length: 529099` and `Access-Control-Allow-Origin: https://pataltimore.github.io`.

## Testing locally

The function is a plain Node handler. With Node 18 or newer:

```bash
node -e "const f=require('./api/ultima4/index.js');const c={log:Object.assign(()=>{},{error:console.log}),res:null};f(c,{headers:{origin:'https://pataltimore.github.io'}}).then(()=>console.log(c.res.status,c.res.body.length))"
```
