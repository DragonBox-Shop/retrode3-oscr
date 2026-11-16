#!/usr/bin/env node
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {extractCatalog,conditionalTokens}=require('../tools/oscr-export-web-menu.js');
const root=process.argv[2]||'third_party/oscr-firmware';
const read=x=>fs.readFileSync(x,'utf8'),sources={};
for(const name of ['NES','MegaDrive','SNES'])sources[name]=read(path.join(root,'src/cores',name+'.cpp'));
const cat=extractCatalog(sources,read(path.join(root,'include/ui/l10n/en.h')),JSON.parse(read(path.join(root,'databases/data/nes-mappers.json'))));
assert.equal(cat.schema_version,1);
assert.equal(cat.fields.nes_mapper.values[0].id,'auto');
assert.ok(cat.fields.nes_mapper.values.length>100);
const nes=cat.menus.find(m=>m.id==='nes.menu-options');
assert.ok(nes.entries.some(e=>e.id==='set-mapper'));
const flash=nes.entries.find(e=>e.id==='write-flash');
assert.equal(flash.risk,'write');assert.equal(flash.implemented,false);
assert.deepEqual(flash.requires,['HAS_FLASH']);
for(const m of cat.menus)assert.equal(new Set(m.entries.map(e=>e.id)).size,m.entries.length);
assert.throws(()=>conditionalTokens('# if HAS_FLASH\nA,\n'),/unterminated/);
console.log(cat.menus.length+' menus and '+(cat.fields.nes_mapper.values.length-1)+' mappers OK');
