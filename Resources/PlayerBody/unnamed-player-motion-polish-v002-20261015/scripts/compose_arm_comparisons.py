from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parent.parent/'previews'
raw=root/'arm_comparison'
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',23)
small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',20)
bold=ImageFont.truetype('C:/Windows/Fonts/seguisb.ttf',33)
names={'A':'Over-shoulder chop','B':'Cross-body backhand','C':'Compact chip'}
notes={
'A':'Wind-up elbow: 108 to 55 degrees. Approach upper-arm lift: 153 to 100 degrees.',
'B':'Arm loads across the chest. Wrist stays cocked until the final approach.',
'C':'Wind-up elbow: 124 to 62 degrees. Approach elbow: 119 to 45 degrees.',
}
for k in 'ABC':
    sheet=Image.new('RGB',(1224,1484),(24,29,36));d=ImageDraw.Draw(sheet)
    d.text((24,16),f'{k}  |  {names[k]}',font=bold,fill=(242,245,249))
    d.text((24,62),'Anticipation and approach  /  matching cameras and frames',font=small,fill=(175,189,203))
    for row,(f,label) in enumerate([(3,'ANTICIPATION  /  FRAME 3'),(6,'APPROACH  /  FRAME 6')]):
        top=98+row*660
        for col,(tag,title) in enumerate([('before','BEFORE'),('after','REVISED')]):
            x=12+col*600
            sheet.paste(Image.open(raw/tag/f'{k}_f{f:02d}_threequarter.png'),(x,top+30))
            d.text((x+12,top+2),f'{title}     {label}',font=small,fill=(140,214,200) if tag=='after' else (222,225,231))
    d.rectangle((0,1422,1224,1484),fill=(24,29,36))
    d.text((24,1438),notes[k],font=small,fill=(220,228,236))
    sheet.save(root/f'{k}_anticipation_approach_before_after.png')
# A compact front-view strip makes the elbow and shoulder spacing easy to compare.
sheet=Image.new('RGB',(1800,1400),(24,29,36));d=ImageDraw.Draw(sheet)
d.text((24,12),'REVISED ARM SILHOUETTES  /  A, B, C',font=bold,fill='white')
for col,k in enumerate('ABC'):
    for row,f in enumerate([3,6]):
        x=600*col;y=70+row*660
        sheet.paste(Image.open(raw/'after'/f'{k}_f{f:02d}_front.png'),(x,y+24))
        d.text((x+16,y),f'{k}   '+('Anticipation - f3' if row==0 else 'Approach - f6'),font=small,fill=(140,214,200))
sheet.save(root/'arm_silhouettes_front.png')
# Filmstrip of the approach, with every 60 fps frame shown.
for tag in ['before','after']:
    sheet=Image.new('RGB',(2100,990),(24,29,36));d=ImageDraw.Draw(sheet)
    for row,k in enumerate('ABC'):
        for col,f in enumerate(range(3,10)):
            im=Image.open(raw/tag/f'{k}_f{f:02d}_threequarter.png').resize((300,320),Image.Resampling.LANCZOS)
            sheet.paste(im,(col*300,row*330+10))
            d.text((col*300+7,row*330+12),f'{k} f{f}',font=small,fill='white',stroke_width=2,stroke_fill=(24,29,36))
    sheet.save(root/f'arm_approach_frames_{tag}.png')
print('Comparison screenshots written to',root)
