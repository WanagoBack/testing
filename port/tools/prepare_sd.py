#!/usr/bin/env python3
"""Prepare local video files for WanagoBack on New 3DS; never downloads from MEGA."""
import argparse, json, pathlib, subprocess, shutil, urllib.request, unicodedata
CATALOG='https://raw.githubusercontent.com/WanagoBack/testing/main/library.json'
def cache_name(entry):
 source=entry.get('megaUrl',entry.get('videoUrl',entry.get('url3ds',''))) or entry['title']
 h=14695981039346656037
 for b in source.encode('utf-8'):h=((h^b)*1099511628211)&((1<<64)-1)
 return f'{h:016x}.mp4'
def normalized(s):return unicodedata.normalize('NFC',s).casefold().strip()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--videos',type=pathlib.Path,required=True,help='Folder with downloaded videos named after their library titles')
 p.add_argument('--sd',type=pathlib.Path,required=True,help='SD card root or a staging folder to copy to the SD card')
 p.add_argument('--library',type=pathlib.Path,help='Optional local library.json, otherwise fetch the configured GitHub catalog')
 p.add_argument('--overwrite',action='store_true')
 p.add_argument('--music',type=pathlib.Path,help='MP3/WAV to loop in menus; converted to menu.wav')
 a=p.parse_args()
 ffmpeg=shutil.which('ffmpeg')
 if not ffmpeg:p.error('FFmpeg is required and must be available in PATH.')
 if not a.videos.is_dir():p.error('--videos must be an existing directory')
 if a.library:catalog=json.loads(a.library.read_text(encoding='utf-8-sig'))
 else:
  with urllib.request.urlopen(CATALOG,timeout=30) as r:catalog=json.load(r)
 entries=catalog.get('videos')
 if not isinstance(entries,list):p.error('library.json must contain a videos array')
 root=a.sd/'3ds'/'WanagoBack';dest=root/'videos';dest.mkdir(parents=True,exist_ok=True)
 # Keep metadata exactly the same so native and preparation cache names match.
 (root/'library.json').write_text(json.dumps(catalog,ensure_ascii=False),encoding='utf-8')
 if a.music:
  music=root/'music';music.mkdir(exist_ok=True)
  subprocess.run([ffmpeg,'-nostdin','-y','-i',str(a.music),'-vn','-c:a','pcm_s16le','-ac','2','-ar','32000',str(music/'menu.wav')],check=True)
 files={}
 for f in a.videos.iterdir():
  if f.is_file() and f.suffix.lower() in {'.mp4','.mkv','.mov','.avi','.webm','.m4v'}:files.setdefault(normalized(f.stem),[]).append(f)
 thumbs=root/'thumbs';thumbs.mkdir(exist_ok=True)
 converted=0;missing=[];ambiguous=[]
 for e in entries:
  title=e.get('title','')
  if not title:continue
  out=dest/cache_name(e)
  if out.exists() and not a.overwrite:
   thumb=thumbs/(out.name+'.png')
   if not thumb.exists():
    subprocess.run([ffmpeg,'-nostdin','-y','-ss','0.1','-i',str(out),'-frames:v','1','-vf','scale=160:96',str(thumb)],check=True)
   continue
  matches=files.get(normalized(title),[])
  if len(matches)>1:ambiguous.append(title);continue
  if not matches:missing.append(title);continue
  temp=out.with_suffix('.partial.mp4')
  command=[ffmpeg,'-nostdin','-y','-i',str(matches[0]),'-map','0:v:0','-map','0:a:0?',
    '-vf','scale=400:240:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=400:240:(ow-iw)/2:(oh-ih)/2,setsar=1',
    '-r','25','-c:v','libx264','-profile:v','baseline','-level:v','3.0','-pix_fmt','yuv420p',
    '-crf','28','-maxrate','500k','-bufsize','1000k','-c:a','aac','-ac','2','-ar','32000','-b:a','64k','-movflags','+faststart',str(temp)]
  try:
   subprocess.run(command,check=True);temp.replace(out);converted+=1
   subprocess.run([ffmpeg,'-nostdin','-y','-ss','0.1','-i',str(out),'-frames:v','1','-vf','scale=160:96',str(thumbs/(out.name+'.png'))],check=True)
  except BaseException:temp.unlink(missing_ok=True);raise
 print(f'{converted} videos prepared. Copy the 3ds folder to the SD card root.')
 if missing:print('No local video found for: '+', '.join(missing))
 if ambiguous:print('Multiple local files match, rename/remove duplicates: '+', '.join(ambiguous))
if __name__=='__main__':main()
