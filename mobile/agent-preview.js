// Exact native-canvas snapshots. No DOM, alternate renderer or model-specific patterns.
function agentPreviewFrame(cells, generation, phase, encode) {
  let x0=Infinity,y0=Infinity,x1=-Infinity,y1=-Infinity;
  for(const p of cells) {
    if(!Array.isArray(p)||p.length!==2||!Number.isSafeInteger(p[0])||!Number.isSafeInteger(p[1]))throw Error('Invalid preview cell');
    x0=Math.min(x0,p[0]);y0=Math.min(y0,p[1]);x1=Math.max(x1,p[0]);y1=Math.max(y1,p[1]);
  }
  if(!cells.length)return {rle:'x = 1, y = 1, rule = B3/S23\n!',population:0,generation,phase,stride:1};
  // Unbounded research can exceed the native world's extent. Preserve occupied
  // bins at an explicitly reported integer display scale, never evolve the bins.
  const stride=Math.max(1,Math.ceil(Math.max(x1-x0+1,y1-y0+1)/16384));
  const display=stride===1?cells:cells.map(([x,y])=>[Math.floor((x-x0)/stride),Math.floor((y-y0)/stride)]);
  return {rle:encode(display).rle,population:cells.length,generation,phase,stride};
}
