from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import imageio_ffmpeg,subprocess
root=Path(__file__).resolve().parent.parent/'previews'
raw=root/'arm_comparison';frames=raw/'video';frames.mkdir(exist_ok=True)
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',24)
for f in range(72):
    canvas=Image.new('RGB',(1280,768),(24,29,36));d=ImageDraw.Draw(canvas)
    for col,(tag,title) in enumerate([('before','BEFORE'),('after','REVISED')]):
        canvas.paste(Image.open(raw/tag/f'chain_{f:03d}.png'),(640*col,48))
        d.text((640*col+20,10),f'{title}   |   {"ABAC"[f//18]}  frame {f%18:02d}',font=font,fill=(140,214,200) if col else (235,238,242))
    canvas.save(frames/f'{f:03d}.png')
ffmpeg=imageio_ffmpeg.get_ffmpeg_exe()
base=root/'arm_ABAC_60fps_single.mp4'
subprocess.run([ffmpeg,'-y','-loglevel','error','-framerate','60','-i',str(frames/'%03d.png'),'-frames:v','72','-c:v','libx264','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(base)],check=True)
subprocess.run([ffmpeg,'-y','-loglevel','error','-stream_loop','3','-i',str(base),'-c','copy','-movflags','+faststart',str(root/'arm_ABAC_before_after_60fps.mp4')],check=True)
subprocess.run([ffmpeg,'-y','-loglevel','error','-framerate','20','-i',str(frames/'%03d.png'),'-frames:v','72','-c:v','libx264','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(root/'arm_ABAC_before_after_one_third_speed.mp4')],check=True)
print('Encoded 60 fps and one-third-speed arm comparisons.')
