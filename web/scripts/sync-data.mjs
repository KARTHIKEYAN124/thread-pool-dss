import { copyFile, mkdir, access, writeFile, readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
const here = fileURLToPath(new URL('../public/data/', import.meta.url));
await mkdir(here, {recursive:true});
const files = [['../results/full_linux/csv/benchmark_results.csv','linux.csv'],['../results/menu/csv/benchmark_results.csv','menu.csv'],['../docs/final_report.md','final_report.md']];
const metadata = {};
for (const [source,name] of files) {
  const target = `${here}/${name}`;
  try { await access(new URL(source, new URL('../',import.meta.url))); await copyFile(new URL(source,new URL('../',import.meta.url)),target); }
  catch { await access(target); }
  const bytes = await readFile(target);
  metadata[name] = {sha256:createHash('sha256').update(bytes).digest('hex'),bytes:bytes.length};
}
await writeFile(`${here}/manifest.json`,JSON.stringify(metadata,null,2)+'\n');
console.log('Benchmark snapshots synced and hashed.');
