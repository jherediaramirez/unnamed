"""Build the local HUD reference from the verified, installed vanilla client.
Run: uv run --with pillow python tools/build_reference.py
Only writes inside this reference directory; never imports into Unreal.
"""
from pathlib import Path
import csv, hashlib, html, io, json, re, urllib.request, zipfile
from collections import Counter
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(r'C:\dev\tmp\Unnamed-HUD-Research-20261007')
SOURCE = WORK / 'source'
JAR = Path(r'C:\Users\jherediaramirez\AppData\Roaming\ModrinthApp\meta\versions\26.3\26.3.jar')
EXPECTED = 'e877b6a07acd633fb3bb475002175cec036e7b87'
sha = lambda b: hashlib.sha256(b).hexdigest()

EFFECTS = {
 'absorption':'Temporary extra health, drawn as additional absorption hearts; distinct from raising maximum normal health.',
 'bad_omen':'Ominous encounter state that can lead to raid or trial omen; classified neutral and placed in the lower effect row.',
 'blindness':'Restricts vision and affects sprint eligibility; the icon indicates an effect, not a health amount.',
 'breath_of_the_nautilus':'Prevents air depletion. By itself it does not refill lost air underwater; Water Breathing or Conduit Power can still refill it.',
 'conduit_power':'Provides underwater breathing and underwater utility. Existing missing air refills while protected.',
 'darkness':'Pulsing restricted visibility; its screen rendering is separate from this icon.',
 'dolphins_grace':'Improves swimming movement; it does not represent oxygen or refill the air meter.',
 'fire_resistance':'Protection against applicable fire damage; do not infer protection against unrelated damage.',
 'glowing':'Entity outline visibility effect; neutral category, therefore lower HUD row.',
 'haste':'Improves mining and attack speed; the item attack indicator still comes from attack timing.',
 'health_boost':'Increases maximum normal health by an attribute; creates more heart containers rather than absorption hearts.',
 'hero_of_the_village':'Village reward/trading effect; its icon is independent of health and hunger.',
 'hunger':'Adds exhaustion and selects the hunger variants of the drumstick textures.',
 'infested':'Can produce silverfish when the affected entity is hurt; this is a harmful effect icon.',
 'instant_damage':'Instant harm effect artwork; ordinary instant application need not create a persistent timed HUD tile.',
 'instant_health':'Instant healing artwork; ordinary instant application need not create a persistent timed HUD tile.',
 'invisibility':'Invisibility effect; also suppresses waypoint transmit range through an attribute modifier.',
 'jump_boost':'Changes jump behavior and safe fall distance; not the mount charge bar.',
 'levitation':'Upward movement effect; distinct from Creative flight.',
 'luck':'Positive luck attribute used by applicable loot calculations; not a generic chance increase for every action.',
 'mining_fatigue':'Slower mining and reduced attack speed; cooldown display remains driven by gameplay state.',
 'nausea':'Screen distortion effect. The separate nausea overlay supports reduced screen-effect intensity.',
 'night_vision':'Improves visibility in darkness; no health or air resource.',
 'oozing':'Produces slimes on death according to effect rules.',
 'poison':'Periodic poison damage and poisoned heart artwork; source heart selection gives poison first priority.',
 'raid_omen':'Raid trigger countdown state; neutral category uses the lower HUD effect row.',
 'regeneration':'Periodic health restoration and the traveling raised-heart animation.',
 'resistance':'Reduces applicable incoming damage; the armor icons do not measure this effect.',
 'saturation':'Restores food/saturation through effect rules; no separate vanilla saturation bar.',
 'slow_falling':'Slower falling and associated fall-damage behavior; independent of air supply.',
 'slowness':'Reduces movement speed through the movement-speed attribute.',
 'speed':'Increases movement speed through the movement-speed attribute.',
 'strength':'Adds attack damage; not armor or health.',
 'trial_omen':'Ominous trial state; neutral category uses the lower HUD effect row.',
 'unluck':'Negative luck attribute for applicable loot calculations.',
 'water_breathing':'Prevents ordinary underwater air depletion and refills missing air.',
 'weakness':'Reduces attack damage.',
 'weaving':'Creates cobwebs on death and participates in cobweb movement rules.',
 'wind_charged':'Triggers a wind burst on death.',
 'wither':'Wither damage and withered hearts; may also select withered art for absorption hearts.',
}
NEUTRAL = {'bad_omen','raid_omen','trial_omen','glowing'}
HARMFUL = {'slowness','mining_fatigue','instant_damage','nausea','blindness','hunger','weakness','poison','wither','levitation','unluck','darkness','wind_charged','weaving','oozing','infested'}

CORE = {
 'air':('Air','Available breath bubble.','Right group at H-49 normally; 9x9 on an 8-unit pitch.','Displayed underwater or until air finishes refilling; full count uses ceil((clampedAir-2)*10/maxAir).','Hud.extractAirBubbles'),
 'air_bursting':('Air','Bubble pop transition.','Same cell as the departing air bubble.','Only underwater when full and popping counts differ; two-tick boundary behavior. Gate the pop sound once per transition.','Hud.extractAirBubbles'),
 'air_empty':('Air','Empty breath position.','Same ten right-aligned cells.','Delayed by one tick during nonzero underwater depletion; ten empty cells wobble when drowning.','Hud.extractAirBubbles'),
 'armor_empty':('Armor','Unfilled armor point pair.','Above the top normal/absorption heart row; x=C-91+8*i.','Show all ten containers only when armor>0. Armor measures protection points, not durability.','Hud.extractArmor'),
 'armor_half':('Armor','One armor point in a two-point icon.','Same armor cells.','Selected when 2*i+1 equals armor.','Hud.extractArmor'),
 'armor_full':('Armor','Two armor points.','Same armor cells.','Selected when 2*i+1 is less than armor.','Hud.extractArmor'),
 'crosshair':('Crosshair','Central aim reticle.','floor((W-15)/2), floor((H-15)/2); drawn 15x15.','First-person visibility and spectator target gates apply; native renderer uses INVERT blending.','Hud.extractCrosshair'),
 'crosshair_attack_indicator_background':('Attack indicator','Empty horizontal attack recharge track.','C-8, floor(H/2)+9; 16x4.','Only Crosshair preference and recharge<1; this is attack recovery, not mining progress.','Hud.extractCrosshair'),
 'crosshair_attack_indicator_progress':('Attack indicator','Filled attack recharge strip.','Overlay the 16x4 track at the same origin.','Crop horizontally to floor(17*attackStrength); do not squash the texture.','Hud.extractCrosshair'),
 'crosshair_attack_indicator_full':('Attack indicator','Ready-to-attack target marker.','Same attack origin, drawn 16x16.','Full charge, living/alive target, attack delay>5 ticks and item range condition. Not always shown at full charge.','Hud.extractCrosshair'),
 'hotbar':('Hotbar','Nine-slot inventory frame.','C-91,H-22; 182x22.','Items are separate 16x16 renderings on a 20-unit pitch. No permanent printed number labels in normal hotbar.','Hud.extractItemHotbar'),
 'hotbar_selection':('Hotbar','Selected-slot border.','C-92+20*slot,H-23; 24x23.','Slot index is 0..8. Overlaps frame border by one unit.','Hud.extractItemHotbar'),
 'hotbar_offhand_left':('Hotbar','Offhand frame on the left.','C-120,H-23; 29x24.','Right-handed main hand; visible only with a nonempty offhand. Item origin C-117,H-19.','Hud.extractItemHotbar'),
 'hotbar_offhand_right':('Hotbar','Offhand frame on the right.','C+91,H-23; 29x24.','Left-handed main hand; visible only with a nonempty offhand. Item origin C+101,H-19.','Hud.extractItemHotbar'),
 'hotbar_attack_indicator_background':('Attack indicator','Empty vertical attack recharge meter.','C+97,H-20 for right main hand; C-113,H-20 for left main hand; 18x18.','Opposite the offhand frame. Only Hotbar preference and recharge<1.','Hud.extractItemHotbar'),
 'hotbar_attack_indicator_progress':('Attack indicator','Vertical attack recharge fill.','Same hotbar attack cell.','Crop from bottom to floor(19*attackStrength) units; keep source UV and destination origin aligned.','Hud.extractItemHotbar'),
 'experience_bar_background':('Context bar','Empty XP progress track.','floor((W-182)/2),H-29; 182x5.','One of XP/locator/mount jump/empty states. XP level is separate text.','ExperienceBar.extractBackground'),
 'experience_bar_progress':('Context bar','Current-level XP progress fill.','Same context track.','Native clipped width floor(183*experienceProgress); progress resets across levels.','ExperienceBar.extractBackground'),
 'jump_bar_background':('Context bar','Mount jump charge track.','Same 182x5 context track at H-29.','For a jumpable vehicle, not ordinary player jumping.','JumpableVehicleBar.extractBackground'),
 'jump_bar_progress':('Context bar','Mount jump charge fill.','Same context track.','Use discrete width: zero at zero; floor(181*p)+1 for positive p.','JumpableVehicleBar.extractBackground'),
 'jump_bar_cooldown':('Context bar','Mount jump cooldown artwork.','Full 182x5 context track.','Cooldown>0 replaces the ordinary charged fill.','JumpableVehicleBar.extractBackground'),
 'locator_bar_background':('Locator','Track behind other players/waypoints.','Same 182x5 context track.','Read GUI scaling metadata; width in the PNG is not necessarily the drawn width.','LocatorBar.extractBackground'),
 'locator_bar_arrow_up':('Locator','Waypoint above camera direction.','Dot x+1, context y-6; 7x5.','Pitch-derived arrow; not an independent waypoint. Respect sprite scaling metadata.','LocatorBar.extractRenderState'),
 'locator_bar_arrow_down':('Locator','Waypoint below camera direction.','Dot x+1, context y+6; 7x5.','Pitch-derived arrow; not an independent waypoint. Respect sprite scaling metadata.','LocatorBar.extractRenderState'),
 'effect_background':('Effects','Ordinary status-effect tile.','24x24 at W-25*n; upper row y=1, lower y=27.','Effect icon goes at +3,+3, 18x18. Nonambient icons fade/pulse in final 200 ticks.','Hud.extractEffects'),
 'effect_background_ambient':('Effects','Ambient status-effect tile.','Same 24x24 effect cells.','Ambient is an effect flag, not a synonym for beneficial.','Hud.extractEffects'),
}
for state in ['empty','half','full']:
 for hungry in [False,True]:
  name='food_'+state+('_hunger' if hungry else '')
  CORE[name]=('Hunger',{'empty':'Empty hunger container.','half':'One food point / half drumstick.','full':'Two food points / full drumstick.'}[state]+(' Hunger-effect artwork.' if hungry else ' Ordinary artwork.'),'Right-to-left at C+82-8*i,H-39; 9x9 on an 8-unit pitch.','Ten icons for 20 food points; replaced by visible mount hearts. Jitters at zero saturation on the source tick schedule.','Hud.extractFood')

def describe(n):
 p=Path(n); s=p.stem
 if '/hud/heart/' in n:
  group='Mount hearts' if s.startswith('vehicle_') else 'Health'
  family=next((a for a in ['absorbing','poisoned','withered','frozen','vehicle','container'] if s.startswith(a)), 'normal')
  amount='empty container' if 'container' in s else ('one point / half heart' if 'half' in s else 'two points / full heart')
  purpose=f'{family.title()} {amount}'+('; Hardcore artwork' if 'hardcore' in s else '; ordinary artwork')+('; blinking variant' if 'blinking' in s else '')+'.'
  placement='Right-to-left at C+82-8*i,H-39-10*row; replaces food.' if group=='Mount hearts' else 'Left-to-right at C-91+8*column,H-39-row*rowHeight; 9x9 with overlapping 8-unit pitch.'
  behavior='Container first, then appropriate full/half fill. Mount hearts are capped at 30 displayed hearts.' if group=='Mount hearts' else 'Select by health/absorption, Hardcore and blink state. Poison > Wither > fully frozen > normal. Absorption occupies extra containers; blinking art is a temporal overlay, not extra health.'
  ue='Use layered sprites and a pure state-to-cell function. Keep damage history, blinking and regeneration offsets separate from authoritative health.'
  return group,purpose,placement,behavior,ue,'Hud.extractHearts / HeartType' if group=='Health' else 'Hud.extractVehicleHealth'
 if '/hud/locator_bar_dot/' in n:
  return 'Locator',f'Waypoint shape {s}; a distance/style variant, not a time-animation frame.','9x9 at ceil((W-9)/2)+floor(yaw*173/120),H-31.','Visible yaw range (-60,+60] degrees. Tint by server-provided color or stable ID; style JSON and distance select shape.','Use replicated visible waypoint data, camera-relative projection, tint, and a shared context bar. Do not discover every actor from the widget.','LocatorBar / WaypointStyle'
 if '/sprites/hud/' in n:
  g,pur,pos,beh,src=CORE[s]
  return g,pur,pos,beh,'Use native dimensions and integer GUI coordinates; clip fills rather than stretch them. Drive visibility and value changes from a HUD snapshot.',src
 if '/mob_effect/' in n:
  category='neutral' if s in NEUTRAL else 'harmful' if s in HARMFUL else 'beneficial'
  return 'Effect icons',EFFECTS[s],f'18x18 within effect tile at +3,+3; {"upper" if category=="beneficial" else "lower"} row ({category}).','Only active effects with showIcon=true. Sort like the source. A stored PNG does not mean the effect is always displayed.','Map an effect ID to this texture plus runtime duration/amplifier/ambient/showIcon data; do not bake duration or level into the PNG.','Hud.extractEffects / MobEffects'
 if '/boss_bar/' in n:
  part='background' if s.endswith('background') else 'progress'
  meaning=f'{s.split("_")[0].title()} boss-bar {part}.' if not s.startswith('notched_') else f'{s.split("_")[1]}-segment boss-bar notch {part}, layered over the chosen color.'
  return 'Boss bars',meaning,'182x5 at C-91,12+19*index; name at bar y-9.','Runtime boss event chooses color, notches, name and progress; stop when stacked offset reaches H/3.','Pool a small event-driven list. Clip both color and notch progress to identical discrete width. Remove events by stable ID.','BossHealthOverlay.extractRenderState / extractBar'
 if '/spectator/' in n:
  return 'Spectator',{'close':'Close spectator selection menu.','scroll_left':'Previous spectator menu page.','scroll_right':'Next spectator menu page.','teleport_to_player':'Select a player to spectate/teleport to.','teleport_to_team':'Select a team category.'}[s],'16x16 within the spectator hotbar item cells.','Spectator menu uses the hotbar frame but different actions, dynamic player heads, labels and fade/slide behavior.','Create a separate spectator presentation policy; never reuse survival inventory ownership for spectator actions.','SpectatorGui / SpectatorMenu'
 if '/toast/' in n or n.endswith('/friends/toast_background.png'):
  purpose={'advancement':'Advancement notification background.','recipe':'Recipe discovery notification background.','system':'System notification background.','tutorial':'Tutorial notification background.','now_playing':'Music notification background.','toast_background':'Friend notification background.','mouse':'Tutorial mouse cue.','movement_keys':'Tutorial movement-key cue.','recipe_book':'Tutorial recipe-book cue.','right_click':'Tutorial use/right-click cue.','social_interactions':'Tutorial social interaction cue.','tree':'Tutorial tree cue.','wooden_planks':'Tutorial planks cue.'}[s]
  return 'Toasts',purpose,'Transient stack sliding in from the top-right; background/tile size comes from each toast.','Queue, lifetime, text, item/player icon and slide animation are runtime. Honor nine-slice metadata when present.','Keep a pooled notification layer and event queue, separate from the persistent effect row and normal gameplay input.','ToastManager / individual toast classes'
 if '/icon/' in n:
  if s.startswith('ping_'):
   desc='Unknown/no measured latency.' if s=='ping_unknown' else 'Player-list connection quality state '+s[-1]+'.'
   return 'Player list',desc,'10x8 at the right edge of the appropriate tab-list row.','Select from measured latency, not health; names, scores, skins and list size remain dynamic.','Read connection state through a model; keep tab visibility on the existing input action.','PlayerTabOverlay.extractPingIcon'
  return 'Chat / notifications',{'chat_modified':'Marker for modified chat content.','draft_report':'Indicator of a saved reporting draft.','music_notes':'Animated music status cue; 16x16 frames in a vertical sheet.'}[s],'Adjacent to its chat/notification content, not in the survival status cluster.','Honor the associated state and animation metadata. Text messages are not PNGs.','Keep this optional layer driven by its real communication/music feature.','ChatComponent / NowPlayingToast / GUI support'
 if '/tooltip/' in n:
  return 'Tooltips',f'Tooltip {s}; skin behind runtime item text.','At dynamically measured tooltip bounds near its anchor, constrained to the viewport.','Honor GUI nine-slice metadata; the PNG is not the tooltip content.','Use measured text and bounded placement with a sliced brush; selected-item HUD name is ordinary text and does not use this frame.','GuiGraphicsExtractor tooltip rendering'
 if 'gamemode_switcher' in n:
  return 'Mode switcher',f'Game-mode switcher {s} artwork.','Centered temporary F3+F4 overlay; not a permanent status display.','Selected mode, permissions, key labels and item representations are runtime.','Implement only when mode switching exists, through existing permission-aware input routing.','GameModeSwitcherScreen (adjacent overlay asset inventory)'
 if '/textures/font/' in n:
  return 'Fonts',{'ascii':'Default Latin bitmap glyph atlas.','accented':'Accented character bitmap glyph atlas.','nonlatin_european':'Additional European/non-Latin glyph atlas.','ascii_sga':'Alternate enchanting alphabet atlas; supporting font option, not normal HUD text.','asciillager':'Alternate illager alphabet atlas; supporting font option.'}[s],'Runtime glyph positions from font provider JSON; most ordinary HUD text uses a 9-unit line height.','Read chars/ascent/height and glyph widths; preserve spacing providers and Unicode fallback dependencies.','Do not stretch the whole sheet as an image. Build an appropriate bitmap font adapter or document a licensed font substitution and compare glyph metrics.','assets/minecraft/font/*.json'
 if '/textures/misc/' in n:
  meanings={'nausea':'Reduced screen-effect nausea overlay.','powder_snow_outline':'Freezing vignette controlled by percent frozen.','pumpkinblur':'Equipped carved-pumpkin camera mask.','spyglass_scope':'Scope opening; black fill outside the scope is procedural.','underwater':'First-person underwater surface texture; separate from air bubbles and water fog.','vignette':'Brightness/world-border vignette.','enchanted_glint_item':'Animated item glint texture; item rendering material support.'}
  return 'Camera / item overlays',meanings[s],'Camera-space/full-screen or item-surface material, not a 9x9 status cell.','Native blend, blur, tint, intensity, UV motion and aspect rules differ per overlay. Respect texture metadata.','Use an appropriate UI/post-process/item material; these are deliberate exceptions to nearest-filtered pixel icons. Match source compositing with a bright/dark-scene capture.','Hud camera overlays / ScreenEffectRenderer / item rendering'
 if '/textures/block/' in n:
  return 'Camera / item overlays',('Animated fire sheet used by first-person fire overlay.' if s.startswith('fire') else 'Animated portal texture used by the portal overlay.'),'Screen-effect geometry/material; source strip contains multiple square frames.','Read animation MCMeta, not the entire strip as one frame. Fire overlay source uses the fire_1 sprite; fire_0 is its related world-fire companion.','Use a flipbook/atlas region material driven by the real burning/portal state; no widget-owned damage.','ScreenEffectRenderer / Hud.extractPortalOverlay'
 if '/textures/item/totem_of_undying' in n:
  return 'Camera / item overlays','Totem item texture supporting the activation animation.','Activation is a moving/scaling item model in screen space, not a static HUD tile.','Texture alone does not reproduce model transforms, animation or the gameplay save-from-death event.','Treat as a deferred event-driven item animation; inventory item rendering is a separate dependency.','Item activation rendering (supporting texture)'
 raise ValueError('Unmapped PNG '+n)

def write(path,data):
 p=ROOT/path;p.parent.mkdir(parents=True,exist_ok=True)
 p.write_bytes(data if isinstance(data,bytes) else data.encode('utf-8'))

def build():
 raw=JAR.read_bytes();assert hashlib.sha1(raw).hexdigest()==EXPECTED
 z=zipfile.ZipFile(io.BytesIO(raw)); official=json.loads((WORK/'official-version.json').read_text())
 index_raw=(WORK/'official-asset-index.json').read_bytes();assert hashlib.sha1(index_raw).hexdigest()==official['assetIndex']['sha1']
 index=json.loads(index_raw)['objects']
 pre=['assets/minecraft/textures/gui/sprites/'+p+'/' for p in ['hud','boss_bar','spectator','toast','tooltip','gamemode_switcher']]
 pre+=['assets/minecraft/textures/mob_effect/','assets/minecraft/textures/font/','assets/minecraft/font/','assets/minecraft/waypoint_style/']
 exact=['assets/minecraft/textures/misc/'+s+'.png' for s in ['nausea','powder_snow_outline','pumpkinblur','spyglass_scope','underwater','vignette','enchanted_glint_item']]
 exact+=['assets/minecraft/textures/gui/sprites/icon/'+s+'.png' for s in ['ping_1','ping_2','ping_3','ping_4','ping_5','ping_unknown','chat_modified','draft_report','music_notes']]
 exact+=['assets/minecraft/textures/gui/sprites/friends/toast_background.png','assets/minecraft/textures/gui/container/gamemode_switcher.png','assets/minecraft/textures/item/totem_of_undying.png']
 exact+=['assets/minecraft/textures/block/'+s+'.png' for s in ['fire_0','fire_1','nether_portal']]
 exact+=['assets/minecraft/atlases/gui.json','assets/minecraft/atlases/mob_effects.json','data/minecraft/enchantment/respiration.json']
 wanted=sorted(n for n in z.namelist() if not n.endswith('/') and (any(n.startswith(p) for p in pre) or n in exact or n.removesuffix('.mcmeta') in exact))
 records=[]
 for n in wanted:
  b=z.read(n);source={'kind':'client.jar','entry':n,'client_sha1':EXPECTED}
  asset_key=n.removeprefix('assets/')
  if asset_key in index:
   obj=index[asset_key]; url=f'https://resources.download.minecraft.net/{obj["hash"][:2]}/{obj["hash"]}'
   b=urllib.request.urlopen(url).read();assert hashlib.sha1(b).hexdigest()==obj['hash'];source={'kind':'asset-index-34','key':asset_key,'sha1':obj['hash'],'url':url}
  write(n,b)
  r={'path':n,'bytes':len(b),'sha256':sha(b),'source':source,'type':Path(n).suffix[1:]}
  if n.endswith('.png'):
   im=Image.open(io.BytesIO(b));im.load();r.update(width=im.width,height=im.height,mode=im.mode)
   g,pur,pos,beh,ue,ref=describe(n);r.update(group=g,purpose=pur,placement=pos,behavior=beh,unreal=ue,reference=ref)
   if n+'.mcmeta' in wanted:r['metadata']=n+'.mcmeta'
  records.append(r)
 # Unicode providers are supplied by the asset index, overriding an empty jar stub.
 for key,obj in index.items():
  n='assets/'+key
  if key.startswith('minecraft/font/') and n not in wanted:
   url=f'https://resources.download.minecraft.net/{obj["hash"][:2]}/{obj["hash"]}'
   b=urllib.request.urlopen(url).read();assert hashlib.sha1(b).hexdigest()==obj['hash'];write(n,b)
   records.append({'path':n,'bytes':len(b),'sha256':sha(b),'source':{'kind':'asset-index-34','key':key,'sha1':obj['hash'],'url':url},'type':Path(n).suffix[1:]})
 png=[r for r in records if r['type']=='png']; counts=dict(sorted(Counter(r['group'] for r in png).items()))
 provenance=json.loads((WORK/'provenance.json').read_text());provenance.update(asset_index=official['assetIndex'],source_root=str(SOURCE),counts=counts,png_count=len(png),file_count=len(records),scope='Research assets only; no Unreal import or gameplay implementation.')
 write('manifest.json',json.dumps({'provenance':provenance,'files':records},indent=2)+'\n')
 write('evidence/provenance.json',json.dumps(provenance,indent=2)+'\n')
 write('evidence/official-version.json',(WORK/'official-version.json').read_bytes())
 write('evidence/official-asset-index.json',index_raw)
 with (ROOT/'manifest.csv').open('w',newline='',encoding='utf-8-sig') as f:
  fields=['path','group','width','height','bytes','sha256','purpose','placement','behavior','unreal','reference'];w=csv.DictWriter(f,fields,extrasaction='ignore');w.writeheader();w.writerows(png)
 guide=['# PNG-by-PNG guide','',f'{len(png)} original PNGs. Paths retain the vanilla namespace; no originals resized or repainted. W/H are GUI dimensions, C=floor(W/2). See HUD-RESEARCH.md for equations and visibility. Each row is also searchable in catalog.html.','']
 for group in counts:
  guide+=['## '+group,'','| PNG / native pixels | Purpose and behavior | Position / Unreal use |','|---|---|---|']
  for r in png:
   if r['group']!=group:continue
   guide.append(f'| [{Path(r["path"]).name}]({r["path"]}) {r["width"]}×{r["height"]} | {r["purpose"]} {r["behavior"]} | {r["placement"]} {r["unreal"]} Source: `{r["reference"]}`. |')
  guide.append('')
 write('PNG-GUIDE.md','\n'.join(guide))
 # Store source identities and method locations without redistributing decompiled game code.
 source_files=[]
 scope=['net/minecraft/client/gui/Hud.java','net/minecraft/client/gui/contextualbar','net/minecraft/client/gui/components/BossHealthOverlay.java','net/minecraft/client/gui/components/PlayerTabOverlay.java','net/minecraft/client/gui/components/ChatComponent.java','net/minecraft/client/gui/components/SubtitleOverlay.java','net/minecraft/client/gui/components/toasts','net/minecraft/client/gui/components/spectator','net/minecraft/client/gui/spectator','net/minecraft/world/entity/Entity.java','net/minecraft/world/entity/LivingEntity.java','net/minecraft/world/entity/player/Player.java','net/minecraft/world/food/FoodData.java','net/minecraft/world/effect','net/minecraft/client/resources/WaypointStyle.java','net/minecraft/client/gui/GuiGraphicsExtractor.java','net/minecraft/client/renderer/RenderPipelines.java','net/minecraft/client/renderer/ScreenEffectRenderer.java','net/minecraft/client/player/LocalPlayer.java','net/minecraft/client/multiplayer/MultiPlayerGameMode.java','com/mojang/blaze3d/platform/Window.java','net/minecraft/util/Mth.java']
 allfiles=[]
 for s in scope:
  p=SOURCE/s
  allfiles.extend(p.rglob('*.java') if p.is_dir() else [p])
 for p in sorted(set(allfiles)):
  if not p.exists():raise FileNotFoundError(p)
  b=p.read_bytes();lines=b.decode().splitlines()
  methods=[{'line':i+1,'declaration':l.strip()} for i,l in enumerate(lines) if re.match(r'\s*(public|private|protected) (static )?(?:final )?[\w<>?,.@]+ \w+\(',l)]
  source_files.append({'relative_path':p.relative_to(SOURCE).as_posix(),'local_path':str(p),'sha256':sha(b),'lines':len(lines),'methods':methods})
 write('evidence/source-map.json',json.dumps(source_files,indent=2)+'\n')
 # Coverage is explicitly bounded to fixed references in the inspected HUD classes.
 scanfiles=[p for p in allfiles if any(a in p.as_posix() for a in ['/Hud.java','/contextualbar/','/BossHealthOverlay.java','/PlayerTabOverlay.java','/ChatComponent.java','/SubtitleOverlay.java','/toasts/','/spectator/','/ScreenEffectRenderer.java'])]
 refs=sorted({a for p in scanfiles for a in re.findall(r'Identifier.withDefaultNamespace\("([^"]+)"\)',p.read_text())})
 covered=[]; non_assets=[];missing=[]
 for ref in refs:
  asset='assets/minecraft/'+ref if ref.startswith('textures/') else 'assets/minecraft/textures/gui/sprites/'+ref+'.png'
  if ref.startswith('internal/'):non_assets.append(ref)
  elif (ROOT/asset).exists():covered.append({'identifier':ref,'path':asset})
  else:missing.append({'identifier':ref,'candidate_path':asset})
 assert not missing,missing
 write('evidence/reference-coverage.json',json.dumps({'fixed_references_covered':covered,'non_texture_action_identifiers':non_assets,'missing':missing,'additional_dynamic_groups':['all 40 mob-effect icons','all locator style PNGs and JSON','font providers and Unicode dependencies','camera equipment pumpkin mask','fire and portal animation textures'],'excluded_dynamic_dependencies':['all inventory item/block/entity models','player skin downloads','arbitrary resource-pack/data-pack assets'],'scope':'Fixed texture identifiers in Hud, context bars, boss/tab/chat/subtitle/toast/spectator and ScreenEffectRenderer. Does not claim all game textures or runtime visual parity.'},indent=2)+'\n')
 build_catalog(png,counts)
 print(json.dumps({'pngs':len(png),'files':len(records),'groups':counts,'fixed_references':len(covered)}))

def build_catalog(png,counts):
 cards=[]
 for r in png:
  a=html.escape; full=r['path']; w,h=r['width'],r['height'];scale=4 if max(w,h)<=32 else 2 if max(w,h)<=192 else 1
  cards.append(f'<article data-search="{a((r["group"]+" "+full+" "+r["purpose"]+" "+r["behavior"]).lower())}"><div class="art"><a href="{full}"><img loading="lazy" src="{full}" style="width:{w*scale}px;max-width:100%;max-height:160px" alt="{a(Path(full).name)}"></a></div><div class="tag">{a(r["group"])} · {w} × {h}</div><h3>{a(Path(full).name)}</h3><p>{a(r["purpose"])}</p><details><summary>Placement, behavior, implementation</summary><p>{a(r["placement"])}</p><p>{a(r["behavior"])}</p><p>{a(r["unreal"])}</p><code>{a(r["reference"])}</code><p class="path">{a(full)}</p></details></article>')
 doc='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Minecraft Java 26.3 HUD asset catalog</title><style>
 :root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#10151c;color:#e6edf3;font:15px/1.5 system-ui}header{padding:30px 5vw 24px;background:#17212d;border-bottom:1px solid #344252}h1{margin:4px 0 10px;font-size:30px}a{color:#88d4fc}nav{display:flex;gap:20px;flex-wrap:wrap}main{padding:24px 5vw}input{width:100%;font:inherit;padding:14px;border:1px solid #537086;border-radius:6px;background:#0b1017;color:white}#count{color:#9bb2c9}#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(255px,1fr));gap:16px}article{background:#19222d;border:1px solid #344252;border-radius:8px;overflow:hidden;padding:15px}.art{height:166px;display:flex;align-items:center;justify-content:center;background:repeating-conic-gradient(#34404c 0% 25%,#29323d 0% 50%) 50%/16px 16px;border-radius:4px;padding:8px;overflow:hidden}img{image-rendering:pixelated;object-fit:contain}.tag{color:#98b5d0;margin-top:12px;font-size:12px}h3{font-size:15px;overflow-wrap:anywhere;margin:6px 0}p{margin:8px 0}summary{cursor:pointer;color:#88d4fc}code,.path{font-size:11px;overflow-wrap:anywhere}details{border-top:1px solid #344252;padding-top:10px}.eyebrow{letter-spacing:.14em;color:#94ddb8;font-size:12px}
 </style><header><div class="eyebrow">UNNAMED / VERIFIED VANILLA REFERENCE</div><h1>Minecraft Java 26.3 · HUD</h1><p>__COUNT__ original PNGs · source-derived placement and behavior · inspected 7 October 2026</p><nav><a href="layout.html">Interactive placement study</a><a href="HUD-RESEARCH.md">Research</a><a href="IMPLEMENT-HUD-IN-UNREAL.txt">Unreal build prompt</a><a href="PNG-GUIDE.md">PNG guide</a><a href="manifest.csv">CSV inventory</a></nav></header><main><input id="search" type="search" aria-label="Filter PNGs" placeholder="Search health, air, locator, poison, filename…"><p id="count"></p><div id="grid">__CARDS__</div></main><script>const cards=[...document.querySelectorAll('article')];function filter(){const q=document.querySelector('#search').value.toLowerCase();let n=0;cards.forEach(c=>{c.hidden=!c.dataset.search.includes(q);if(!c.hidden)n++});document.querySelector('#count').textContent=n+' of '+cards.length+' assets'}document.querySelector('#search').addEventListener('input',filter);filter();</script></html>'''
 write('catalog.html',doc.replace('__COUNT__',str(len(png))).replace('__CARDS__',''.join(cards)))

if __name__=='__main__':build()
