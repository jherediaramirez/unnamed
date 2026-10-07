import fs from 'node:fs';
import path from 'node:path';

export function readCaptureEvidence(capturesRoot, smokeName, videoName) {
  if (![smokeName,videoName].every(name=>/^[A-Za-z0-9_-]+$/.test(name))) throw Error('Invalid capture name');
  const read=(name,file)=>JSON.parse(fs.readFileSync(path.join(capturesRoot,name,file),'utf8'));
  const run=read(smokeName,'results.json'), video=read(videoName,'results.json');
  if(!run.htmlSHA256||run.htmlSHA256!==video.htmlSHA256||run.profile!==video.profile||
     !run.checks.length||!video.checks.length||[...run.checks,...video.checks].some(c=>!c.pass))
    throw Error('Registry requires matching, passing smoke and video captures of the same HTML/profile');
  return {run,data:read(smokeName,'definitions.json'),extras:read(videoName,'VideoResetControls.json')};
}
