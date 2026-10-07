# Screen Inventory

The working interface retains 33 renderer IDs. Minecraft Realms, Join Realm, their entry points and the Realms news/invites preference were deliberately removed. The mock Game closed route was removed for normal Quit Game; the immutable reference retains all original renderers and assets. Create has Game, World and More tabs. Friends, connection, creation, backup/folder and clipboard actions use the source message/confirmation routes without services.

| Route | Expected entry | Return |
|---|---|---|
| main | Boot | Source parent via Back/Done/Cancel/Escape; main remains main |
| options | main | Source parent via Back/Done/Cancel/Escape; main remains main |
| video | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| sounds | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| controls | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| accessibility | main/options | Source parent via Back/Done/Cancel/Escape; main remains main |
| online | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| language | main/options | Source parent via Back/Done/Cancel/Escape; main remains main |
| keybinds | controls | Source parent via Back/Done/Cancel/Escape; main remains main |
| worlds | main | Source parent via Back/Done/Cancel/Escape; main remains main |
| create | worlds | Source parent via Back/Done/Cancel/Escape; main remains main |
| editworld | worlds (selected) | Source parent via Back/Done/Cancel/Escape; main remains main |
| safety | main Multiplayer | Source parent via Back/Done/Cancel/Escape; main remains main |
| multiplayer | safety/skip warning | Source parent via Back/Done/Cancel/Escape; main remains main |
| direct | multiplayer | Source parent via Back/Done/Cancel/Escape; main remains main |
| serveredit | multiplayer (Add/Edit) | Source parent via Back/Done/Cancel/Escape; main remains main |
| packs | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| datapacks | create/more | Source parent via Back/Done/Cancel/Escape; main remains main |
| experiments | create/more | Source parent via Back/Done/Cancel/Escape; main remains main |
| rules | create/more | Source parent via Back/Done/Cancel/Escape; main remains main |
| flat | create/world (Superflat) | Source parent via Back/Done/Cancel/Escape; main remains main |
| presets | flat | Source parent via Back/Done/Cancel/Escape; main remains main |
| biomes | create/world (Single Biome) | Source parent via Back/Done/Cancel/Escape; main remains main |
| credits | main/options | Source parent via Back/Done/Cancel/Escape; main remains main |
| creditstext | credits | Source parent via Back/Done/Cancel/Escape; main remains main |
| telemetry | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| linkconfirm | accessibility/online/credits/telemetry | Source parent via Back/Done/Cancel/Escape; main remains main |
| message | reversible service endpoints | Source parent via Back/Done/Cancel/Escape; main remains main |
| confirm | friends and sample deletion | Source parent via Back/Done/Cancel/Escape; main remains main |
| skin | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| chat | options | Source parent via Back/Done/Cancel/Escape; main remains main |
| mouse | controls | Source parent via Back/Done/Cancel/Escape; main remains main |
| font | language | Source parent via Back/Done/Cancel/Escape; main remains main |

Observed route edges are recorded in the smoke results. Programmatic preparation is used to cover isolated renderers; native browser mouse/keyboard and targeted nested routes are tested separately.
