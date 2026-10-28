# ThreadPool Lab web explorer

Live production site: https://threadpool-lab.vercel.app

React and Vite dashboard for the C++ vehicle sensor benchmark. Vercel hosts recorded measurements; native C++ execution remains local.

```sh
cd web
npm ci
npm run build
npm test
npm run dev
```

The build copies the Linux and menu summary CSVs and final report from the parent project when present. Committed snapshots in `public/data` allow standalone Vercel builds. SHA-256 hashes are recorded in `public/data/manifest.json`. To refresh menu measurements, finish the local experiment, rebuild, and deploy; visitors can also load compatible summary CSVs directly in the browser. Uploads are never sent to a server.

The Explorer keeps dataset sources separate and groups by full experiment configuration. Chart averages exclude incorrect runs. Reported p99 averages summarize per-run percentiles rather than a pooled latency distribution. The Methodology tab documents measurement limitations.

Vercel project: `karthikeyan124s-projects/threadpool-lab`. From this directory use `vercel project inspect --non-interactive` to confirm the target, then `vercel deploy --prod` to update the site. Keep `.vercel` and `.env*` files out of version control.
