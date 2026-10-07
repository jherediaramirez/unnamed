# Control Registry

Generated from rendered controls in the packaged browser, plus the supplied structured definitions. No real feature is marked implemented. `ControlRegistry.json` contains defaults, ranges, dependencies, transactions, source meaning, proposed integration, later acceptance and unresolved questions for every entry.

Observed 706 distinct controls across 36 source routes and Create tabs. Dynamic selected-pack controls are included in the supplemental observations.

| Screen | Stable ID | Exact observed label | Widget | Default | Status |
|---|---|---|---|---|---|
| main | main..Singleplayer | Singleplayer | button | "Not applicable (command)" | UI_ONLY |
| main | main..Multiplayer | Multiplayer | button | "Not applicable (command)" | UI_ONLY |
| main | main..Minecraft_Realms | Minecraft Realms | button | "Not applicable (command)" | UI_ONLY |
| main | main..Friends_List | Friends List | button | "Not applicable (command)" | UI_ONLY |
| main | main..Language | Language | button | "" | UI_ONLY |
| main | main..Accessibility_Settings | Accessibility Settings | button | "Not applicable (command)" | UI_ONLY |
| main | main..Options... | Options... | button | "Not applicable (command)" | UI_ONLY |
| main | main..Quit_Game | Quit Game | button | "Not applicable (command)" | UI_ONLY |
| main | main..Copyright_Mojang_AB._Do_not_distribute_ | Copyright Mojang AB. Do not distribute! | button | "Not applicable (command)" | UI_ONLY |
| options | options.fov | FOV | range | 70 | UI_ONLY |
| options | options..Online... | Online... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Skin_Customization... | Skin Customization... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Music___Sounds... | Music & Sounds... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Video_Settings... | Video Settings... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Controls... | Controls... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Language... | Language... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Chat_Settings... | Chat Settings... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Resource_Packs... | Resource Packs... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Accessibility_Settings... | Accessibility Settings... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Telemetry_Data... | Telemetry Data... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Credits___Attribution... | Credits & Attribution... | button | "Not applicable (command)" | UI_ONLY |
| options | options..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| video | video..Exclusive_Fullscreen_Mode | Exclusive Fullscreen Mode | range | "0" | INTENTIONALLY_DISABLED |
| video | video.framerateLimit | Max Framerate | range | 120 | UI_ONLY |
| video | video.enableVsync | VSync: ON | button | true | UI_ONLY |
| video | video.inactivityFpsLimit | Reduce fps when: AFK | button | "AFK" | UI_ONLY |
| video | video.guiScale | GUI Scale: Auto | button | 0 | UI_ONLY |
| video | video.fullscreen | Fullscreen: OFF | button | false | UI_ONLY |
| video | video.exclusiveFullscreen | Exclusive Fullscreen: OFF | button | false | UI_ONLY |
| video | video.gamma | Brightness | range | 0.5 | UI_ONLY |
| video | video.preferredGraphicsBackend | Graphics API: Default | button | "Default" | UI_ONLY |
| video | video.graphicsPreset | Preset | range | "Fancy" | UI_ONLY |
| video | video.biomeBlendRadius | Biome Blend | range | 2 | UI_ONLY |
| video | video.renderDistance | Render Distance | range | 12 | UI_ONLY |
| video | video.prioritizeChunkUpdates | Chunk Builder: Threaded | button | "Threaded" | UI_ONLY |
| video | video.simulationDistance | Simulation Distance | range | 12 | UI_ONLY |
| video | video.ambientOcclusion | Smooth Lighting: ON | button | true | UI_ONLY |
| video | video.cloudStatus | Clouds: Fancy | button | "Fancy" | UI_ONLY |
| video | video.particles | Particles: All | button | "All" | UI_ONLY |
| video | video.mipmapLevels | Mipmap Levels | range | 4 | UI_ONLY |
| video | video.entityShadows | Entity Shadows: ON | button | true | UI_ONLY |
| video | video.entityDistanceScaling | Entity Distance | range | 1 | UI_ONLY |
| video | video.menuBackgroundBlurriness | Menu Background Blur | range | 5 | UI_ONLY |
| video | video.cloudRange | Cloud Distance | range | 128 | UI_ONLY |
| video | video.cutoutLeaves | See-Through Leaves: ON | button | true | UI_ONLY |
| video | video.improvedTransparency | Improved Transparency: OFF | button | false | UI_ONLY |
| video | video.textureFiltering | Texture Filtering: None | button | "None" | UI_ONLY |
| video | video.maxAnisotropyBit | Anisotropic Filtering | range | 2 | UI_ONLY |
| video | video.weatherRadius | Weather Effect Radius | range | 10 | UI_ONLY |
| video | video.showAutosaveIndicator | Autosave Indicator: ON | button | true | UI_ONLY |
| video | video.vignette | Show Vignette: ON | button | true | UI_ONLY |
| video | video.attackIndicator | Attack Indicator: Crosshair | button | "Crosshair" | UI_ONLY |
| video | video.chunkSectionFadeInTime | Chunk Fade Time | range | 0.75 | UI_ONLY |
| video | video..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| video | video..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| sounds | sounds.sound_master | Master Volume | range | 1 | UI_ONLY |
| sounds | sounds.sound_music | Music | range | 1 | UI_ONLY |
| sounds | sounds.sound_record | Jukebox/Note Blocks | range | 1 | UI_ONLY |
| sounds | sounds.sound_weather | Weather | range | 1 | UI_ONLY |
| sounds | sounds.sound_block | Blocks | range | 1 | UI_ONLY |
| sounds | sounds.sound_hostile | Hostile Mobs | range | 1 | UI_ONLY |
| sounds | sounds.sound_neutral | Friendly Mobs | range | 1 | UI_ONLY |
| sounds | sounds.sound_player | Players | range | 1 | UI_ONLY |
| sounds | sounds.sound_ambient | Ambient/Environment | range | 1 | UI_ONLY |
| sounds | sounds.sound_voice | Narrator/Voice | range | 1 | UI_ONLY |
| sounds | sounds.sound_ui | UI | range | 1 | UI_ONLY |
| sounds | sounds.soundDevice | System Default | button | "System Default" | UI_ONLY |
| sounds | sounds.showSubtitles | Closed Captions: OFF | button | false | UI_ONLY |
| sounds | sounds.directionalAudio | Directional Audio: OFF | button | false | UI_ONLY |
| sounds | sounds.musicFrequency | Music Frequency: Default | button | "Default" | UI_ONLY |
| sounds | sounds.musicToast | Music Toast: Never | button | "Never" | UI_ONLY |
| sounds | sounds..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| sounds | sounds..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| controls | controls..Mouse_Settings... | Mouse Settings... | button | "Not applicable (command)" | UI_ONLY |
| controls | controls..Key_Binds... | Key Binds... | button | "Not applicable (command)" | UI_ONLY |
| controls | controls.toggleCrouch | Sneak: Hold | button | false | UI_ONLY |
| controls | controls.toggleSprint | Sprint: Hold | button | false | UI_ONLY |
| controls | controls.toggleAttack | Attack/Destroy: Hold | button | false | UI_ONLY |
| controls | controls.toggleUse | Use Item/Place Block: Hold | button | false | UI_ONLY |
| controls | controls.autoJump | Auto-Jump: OFF | button | false | UI_ONLY |
| controls | controls.sprintWindow | Sprint Window | range | 7 | UI_ONLY |
| controls | controls.quitShortcuts | Quit Shortcuts: ON | button | true | UI_ONLY |
| controls | controls.operatorItemsTab | Operator Items Tab: OFF | button | false | UI_ONLY |
| controls | controls..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| controls | controls..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| accessibility | accessibility.narrator | Narrator: OFF | button | "OFF" | UI_ONLY |
| accessibility | accessibility..Controls... | Controls... | button | "Not applicable (command)" | UI_ONLY |
| accessibility | accessibility.showSubtitles | Closed Captions: ON | button | false | UI_ONLY |
| accessibility | accessibility.highContrast | High Contrast: OFF | button | false | UI_ONLY |
| accessibility | accessibility.menuBackgroundBlurriness | Menu Background Blur | range | 5 | UI_ONLY |
| accessibility | accessibility.textBackgroundOpacity | Text Background Opacity | range | 0.5 | UI_ONLY |
| accessibility | accessibility.backgroundForChatOnly | Chat | button | true | UI_ONLY |
| accessibility | accessibility.chatOpacity | Chat Text Opacity | range | 1 | UI_ONLY |
| accessibility | accessibility.chatLineSpacing | Line Spacing | range | 0 | UI_ONLY |
| accessibility | accessibility.chatDelay | options.chat.delay_instant | range | 0 | UI_ONLY |
| accessibility | accessibility.notificationDisplayTime | Notification Time | range | 1 | UI_ONLY |
| accessibility | accessibility.bobView | View Bobbing: ON | button | true | UI_ONLY |
| accessibility | accessibility.screenEffectScale | Distortion Effects | range | 1 | UI_ONLY |
| accessibility | accessibility.fovEffectScale | FOV Effects | range | 1 | UI_ONLY |
| accessibility | accessibility.darknessEffectScale | Darkness Pulsing | range | 1 | UI_ONLY |
| accessibility | accessibility.damageTiltStrength | Damage Tilt | range | 1 | UI_ONLY |
| accessibility | accessibility.glintSpeed | Glint Speed | range | 0.5 | UI_ONLY |
| accessibility | accessibility.glintStrength | Glint Strength | range | 0.75 | UI_ONLY |
| accessibility | accessibility.hideLightningFlash | Hide Sky Flashes: OFF | button | false | UI_ONLY |
| accessibility | accessibility.darkMojangStudiosBackground | Monochrome Logo: OFF | button | false | UI_ONLY |
| accessibility | accessibility.panoramaSpeed | Panorama Scroll Speed | range | 1 | UI_ONLY |
| accessibility | accessibility.hideSplashTexts | Hide Splash Texts: OFF | button | false | UI_ONLY |
| accessibility | accessibility.narratorHotkey | Narrator Hotkey: ON | button | true | UI_ONLY |
| accessibility | accessibility.rotateWithMinecart | Rotate with Minecarts: OFF | button | false | INTENTIONALLY_DISABLED |
| accessibility | accessibility.highContrastBlockOutline | High Contrast Block Outlines: OFF | button | false | UI_ONLY |
| accessibility | accessibility..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| accessibility | accessibility..Accessibility_Guide | Accessibility Guide | button | "Not applicable (command)" | UI_ONLY |
| accessibility | accessibility..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| online | online.friendsList | Friends List: OFF | button | false | UI_ONLY |
| online | online.allowFriendRequests | Allow Requests: OFF | button | false | UI_ONLY |
| online | online.inGameNotification | In-Game Notification: OFF | button | false | UI_ONLY |
| online | online.sharePresence | Visibility: Full | button | "Full" | UI_ONLY |
| online | online..Xbox_Settings... | Xbox Settings... | button | "Not applicable (command)" | UI_ONLY |
| online | online.allowServerListing | Allow Server Listings: ON | button | true | UI_ONLY |
| online | online.realmsNotifications | News & Invites: ON | button | true | UI_ONLY |
| online | online..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| online | online..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| language | language..Language | Language | text | "" | UI_ONLY |
| language | language.af_za | Afrikaans (Suid-Afrika) | option | "false" | UI_ONLY |
| language | language.ar_sa | العربية (العالم العربي) | option | "false" | UI_ONLY |
| language | language.ast_es | Asturianu (Asturies) | option | "false" | UI_ONLY |
| language | language.az_az | Azərbaycanca (Azərbaycan) | option | "false" | UI_ONLY |
| language | language.ba_ru | Башҡортса (Башҡортостан, Рәсәй) | option | "false" | UI_ONLY |
| language | language.bar | Boarisch (Bayern) | option | "false" | UI_ONLY |
| language | language.be_by | Беларуская (Беларусь) | option | "false" | UI_ONLY |
| language | language.be_latn | Biełaruskaja (Biełaruś) | option | "false" | UI_ONLY |
| language | language.bg_bg | Български (България) | option | "false" | UI_ONLY |
| language | language.br_fr | Brezhoneg (Breizh) | option | "false" | UI_ONLY |
| language | language.brb | Braobans (Braobant) | option | "false" | UI_ONLY |
| language | language.bs_ba | Bosanski (Bosna i Hercegovina) | option | "false" | UI_ONLY |
| language | language.ca_es | Català (Catalunya) | option | "false" | UI_ONLY |
| language | language.cs_cz | Čeština (Česko) | option | "false" | UI_ONLY |
| language | language.cv_cu | Чӑвашла (Чӑваш Ен, Раҫҫей) | option | "false" | UI_ONLY |
| language | language.cy_gb | Cymraeg (Cymru) | option | "false" | UI_ONLY |
| language | language.da_dk | Dansk (Danmark) | option | "false" | UI_ONLY |
| language | language.de_at | Deitsch (Österreich) | option | "false" | UI_ONLY |
| language | language.de_ch | Schwiizerdütsch (Schwiiz) | option | "false" | UI_ONLY |
| language | language.de_de | Deutsch (Deutschland) | option | "false" | UI_ONLY |
| language | language.el_gr | Ελληνικά (Ελλάδα) | option | "false" | UI_ONLY |
| language | language.en_au | English (Australia) | option | "false" | UI_ONLY |
| language | language.en_ca | English (Canada) | option | "false" | UI_ONLY |
| language | language.en_gb | English (United Kingdom) | option | "false" | UI_ONLY |
| language | language.en_nz | English (New Zealand) | option | "false" | UI_ONLY |
| language | language.en_pt | Pirate Speak (The Seven Seas) | option | "false" | UI_ONLY |
| language | language.en_ud | ɥsᴉꞁᵷuƎ (uʍoᗡ ǝpᴉsd∩) | option | "false" | UI_ONLY |
| language | language.en_us | English (US) | option | "true" | UI_ONLY |
| language | language.enp | Anglish (Oned Riches) | option | "false" | UI_ONLY |
| language | language.enws | Shakespearean English (Kingdom of England) | option | "false" | UI_ONLY |
| language | language.eo_uy | Esperanto (Esperantujo) | option | "false" | UI_ONLY |
| language | language.es_ar | Español (Argentina) | option | "false" | UI_ONLY |
| language | language.es_cl | Español (Chile) | option | "false" | UI_ONLY |
| language | language.es_ec | Español (Ecuador) | option | "false" | UI_ONLY |
| language | language.es_es | Español (España) | option | "false" | UI_ONLY |
| language | language.es_mx | Español (México) | option | "false" | UI_ONLY |
| language | language.es_uy | Español (Uruguay) | option | "false" | UI_ONLY |
| language | language.es_ve | Español (Venezuela) | option | "false" | UI_ONLY |
| language | language.esan | Andalûh (Andaluçía) | option | "false" | UI_ONLY |
| language | language.et_ee | Eesti keel (Eesti) | option | "false" | UI_ONLY |
| language | language.eu_es | Euskara (Euskal Herria) | option | "false" | UI_ONLY |
| language | language.fa_ir | فارسی (ايران) | option | "false" | UI_ONLY |
| language | language.fi_fi | Suomi (Suomi) | option | "false" | UI_ONLY |
| language | language.fil_ph | Filipino (Pilipinas) | option | "false" | UI_ONLY |
| language | language.fo_fo | Føroyskt (Føroyar) | option | "false" | UI_ONLY |
| language | language.fr_ca | Français (Canada) | option | "false" | UI_ONLY |
| language | language.fr_ch | Français (Suisse) | option | "false" | UI_ONLY |
| language | language.fr_fr | Français (France) | option | "false" | UI_ONLY |
| language | language.fra_de | Fränggisch (Franggn) | option | "false" | UI_ONLY |
| language | language.fur_it | Furlan (Friûl) | option | "false" | UI_ONLY |
| language | language.fy_nl | Frysk (Fryslân) | option | "false" | UI_ONLY |
| language | language.ga_ie | Gaeilge (Éire) | option | "false" | UI_ONLY |
| language | language.gd_gb | Gàidhlig (Alba) | option | "false" | UI_ONLY |
| language | language.gl_es | Galego (Galicia / Galiza) | option | "false" | UI_ONLY |
| language | language.go_fr | Galo (Bertègn) | option | "false" | UI_ONLY |
| language | language.got_de | 𐌲𐌿𐍄𐍂𐌰𐌶𐌳𐌰 (𐌲𐌿𐍄𐌸𐌹𐌿𐌳𐌰) | option | "false" | UI_ONLY |
| language | language.hal_ua | Галицка (Галичина, Вкраїна) | option | "false" | UI_ONLY |
| language | language.haw_us | ʻŌlelo Hawaiʻi (Hawaiʻi) | option | "false" | UI_ONLY |
| language | language.he_il | עברית (ישראל) | option | "false" | UI_ONLY |
| language | language.hi_in | हिंदी (भारत) | option | "false" | UI_ONLY |
| language | language.hn_no | Høgnorsk (Norig) | option | "false" | UI_ONLY |
| language | language.hr_hr | Hrvatski (Hrvatska) | option | "false" | UI_ONLY |
| language | language.hu_hu | Magyar (Magyarország) | option | "false" | UI_ONLY |
| language | language.hy_am | Հայերեն (Հայաստան) | option | "false" | UI_ONLY |
| language | language.id_id | Bahasa Indonesia (Indonesia) | option | "false" | UI_ONLY |
| language | language.ig_ng | Igbo (Naigeria) | option | "false" | UI_ONLY |
| language | language.io_en | Ido (Idia) | option | "false" | UI_ONLY |
| language | language.is_is | Íslenska (Ísland) | option | "false" | UI_ONLY |
| language | language.isv | Medžuslovjansky (Slovjanščina) | option | "false" | UI_ONLY |
| language | language.it_it | Italiano (Italia) | option | "false" | UI_ONLY |
| language | language.ja_jp | 日本語 (日本) | option | "false" | UI_ONLY |
| language | language.jbo_en | la .lojban. (la jbogu'e) | option | "false" | UI_ONLY |
| language | language.ka_ge | ქართული (საქართველო) | option | "false" | UI_ONLY |
| language | language.kk_kz | Қазақша (Қазақстан) | option | "false" | UI_ONLY |
| language | language.kn_in | ಕನ್ನಡ (ಭಾರತ) | option | "false" | UI_ONLY |
| language | language.ko_kr | 한국어 (대한민국) | option | "false" | UI_ONLY |
| language | language.ksh | Kölsch/Ripoarisch (Rhingland) | option | "false" | UI_ONLY |
| language | language.kw_gb | Kernewek (Kernow) | option | "false" | UI_ONLY |
| language | language.ky_kg | Кыргызча (Кыргызстан) | option | "false" | UI_ONLY |
| language | language.la_la | Latina (Latium) | option | "false" | UI_ONLY |
| language | language.lb_lu | Lëtzebuergesch (Lëtzebuerg) | option | "false" | UI_ONLY |
| language | language.li_li | Limburgs (Limburg) | option | "false" | UI_ONLY |
| language | language.lmo | Lombard (Lombardia) | option | "false" | UI_ONLY |
| language | language.lo_la | ລາວ (ປະເທດລາວ) | option | "false" | UI_ONLY |
| language | language.lol_us | LOLCAT (Kingdom of Cats) | option | "false" | UI_ONLY |
| language | language.lt_lt | Lietuvių (Lietuva) | option | "false" | UI_ONLY |
| language | language.lv_lv | Latviešu (Latvija) | option | "false" | UI_ONLY |
| language | language.lzh | 文言 (華夏) | option | "false" | UI_ONLY |
| language | language.mk_mk | Македонски (Северна Македонија) | option | "false" | UI_ONLY |
| language | language.mn_mn | Монгол (Монгол Улс) | option | "false" | UI_ONLY |
| language | language.ms_my | Bahasa Melayu (Malaysia) | option | "false" | UI_ONLY |
| language | language.mt_mt | Malti (Malta) | option | "false" | UI_ONLY |
| language | language.nah | Mēxikatlahtōlli (Mēxiko) | option | "false" | UI_ONLY |
| language | language.nds_de | Plattdüütsch (Düütschland) | option | "false" | UI_ONLY |
| language | language.nl_be | Vlaams (België) | option | "false" | UI_ONLY |
| language | language.nl_nl | Nederlands (Nederland) | option | "false" | UI_ONLY |
| language | language.nn_no | Norsk nynorsk (Noreg) | option | "false" | UI_ONLY |
| language | language.no_no | Norsk bokmål (Norge) | option | "false" | UI_ONLY |
| language | language.oc_fr | Occitan (Occitània) | option | "false" | UI_ONLY |
| language | language.ovd | Övdalska (Swerre) | option | "false" | UI_ONLY |
| language | language.pl_pl | Polski (Polska) | option | "false" | UI_ONLY |
| language | language.pls | Ngiiwà (Ndanìꞌngà) | option | "false" | UI_ONLY |
| language | language.pt_br | Português (Brasil) | option | "false" | UI_ONLY |
| language | language.pt_pt | Português (Portugal) | option | "false" | UI_ONLY |
| language | language.qcb_es | Cántabru/Montañés (Cantabria) | option | "false" | UI_ONLY |
| language | language.qid | Bhs. Indonesia edjaän lama (Indonesia tempo doeloe) | option | "false" | UI_ONLY |
| language | language.qya_aa | Quenya (Arda) | option | "false" | UI_ONLY |
| language | language.ro_ro | Română (România) | option | "false" | UI_ONLY |
| language | language.rpr | Русскій дореформенный (Россійская имперія) | option | "false" | UI_ONLY |
| language | language.ru_ru | Русский (Россия) | option | "false" | UI_ONLY |
| language | language.ry_ua | Руснацькый (Пудкарпатя, Украина) | option | "false" | UI_ONLY |
| language | language.sah_sah | Сахалыы (Cаха Сирэ) | option | "false" | UI_ONLY |
| language | language.se_no | Davvisámegiella (Sápmi) | option | "false" | UI_ONLY |
| language | language.sk_sk | Slovenčina (Slovensko) | option | "false" | UI_ONLY |
| language | language.sl_si | Slovenščina (Slovenija) | option | "false" | UI_ONLY |
| language | language.so_so | Soomaali (Soomaaliya) | option | "false" | UI_ONLY |
| language | language.sq_al | Shqip (Shqipëri) | option | "false" | UI_ONLY |
| language | language.sr_cs | Srpski (Srbija) | option | "false" | UI_ONLY |
| language | language.sr_sp | Српски (Србија) | option | "false" | UI_ONLY |
| language | language.sv_se | Svenska (Sverige) | option | "false" | UI_ONLY |
| language | language.sxu | Säggs’sch (Saggsn) | option | "false" | UI_ONLY |
| language | language.szl | Ślōnski (Gōrny Ślōnsk) | option | "false" | UI_ONLY |
| language | language.ta_in | தமிழ் (இந்தியா) | option | "false" | UI_ONLY |
| language | language.th_th | ไทย (ประเทศไทย) | option | "false" | UI_ONLY |
| language | language.tl_ph | Tagalog (Pilipinas) | option | "false" | UI_ONLY |
| language | language.tlh_aa | tlhIngan Hol (tlhIngan wo') | option | "false" | UI_ONLY |
| language | language.tok | toki pona (kulupu pona) | option | "false" | UI_ONLY |
| language | language.tr_tr | Türkçe (Türkiye) | option | "false" | UI_ONLY |
| language | language.tt_ru | Татарча (Татарстан, Рәсәй) | option | "false" | UI_ONLY |
| language | language.tzo_mx | Bats'i k'op (Jobel) | option | "false" | UI_ONLY |
| language | language.uk_ua | Українська (Україна) | option | "false" | UI_ONLY |
| language | language.uz_uz | O'zbekcha (O'zbekiston) | option | "false" | UI_ONLY |
| language | language.val_es | Català (Valencià) (País Valencià) | option | "false" | UI_ONLY |
| language | language.vec_it | Vèneto (Veneto) | option | "false" | UI_ONLY |
| language | language.vi_vn | Tiếng Việt (Việt Nam) | option | "false" | UI_ONLY |
| language | language.vp_vl | Viossa (Vilant) | option | "false" | UI_ONLY |
| language | language.vro | Võro (Eesti) | option | "false" | UI_ONLY |
| language | language.yi_de | ייִדיש (אשכנזיש יידן) | option | "false" | UI_ONLY |
| language | language.yo_ng | Yorùbá (Nàìjíríà) | option | "false" | UI_ONLY |
| language | language.zh_cn | 简体中文 (中国大陆) | option | "false" | UI_ONLY |
| language | language.zh_hk | 繁體中文 (香港特別行政區) | option | "false" | UI_ONLY |
| language | language.zh_tw | 繁體中文 (台灣) | option | "false" | UI_ONLY |
| language | language.zlm_arab | بهاس ملايو (مليسيا) | option | "false" | UI_ONLY |
| language | language..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| language | language..Font_Settings... | Font Settings... | button | "Not applicable (command)" | UI_ONLY |
| language | language..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.jump | Space | button | "Space" | UI_ONLY |
| keybinds | keybinds..Reset | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.sneak | Left Shift | button | "Left Shift" | UI_ONLY |
| keybinds | keybinds..Reset.1 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.sprint | Left Control | button | "Left Control" | UI_ONLY |
| keybinds | keybinds..Reset.2 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.left | A | button | "A" | UI_ONLY |
| keybinds | keybinds..Reset.3 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.right | D | button | "D" | UI_ONLY |
| keybinds | keybinds..Reset.4 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.back | S | button | "S" | UI_ONLY |
| keybinds | keybinds..Reset.5 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.forward | W | button | "W" | UI_ONLY |
| keybinds | keybinds..Reset.6 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.advancements | L | button | "L" | UI_ONLY |
| keybinds | keybinds..Reset.7 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.quickActions | G | button | "G" | UI_ONLY |
| keybinds | keybinds..Reset.8 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.screenshot | F2 | button | "F2" | UI_ONLY |
| keybinds | keybinds..Reset.9 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.smoothCamera | Not Bound | button | "Not Bound" | UI_ONLY |
| keybinds | keybinds..Reset.10 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.fullscreen | F11 | button | "F11" | UI_ONLY |
| keybinds | keybinds..Reset.11 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.toggleGui | F1 | button | "F1" | UI_ONLY |
| keybinds | keybinds..Reset.12 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.togglePerspective | F5 | button | "F5" | UI_ONLY |
| keybinds | keybinds..Reset.13 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.toggleSpectatorShaderEffects | F4 | button | "F4" | UI_ONLY |
| keybinds | keybinds..Reset.14 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.friends | O | button | "O" | UI_ONLY |
| keybinds | keybinds..Reset.15 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.playerlist | Tab | button | "Tab" | UI_ONLY |
| keybinds | keybinds..Reset.16 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.chat | T | button | "T" | UI_ONLY |
| keybinds | keybinds..Reset.17 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.command | / | button | "/" | UI_ONLY |
| keybinds | keybinds..Reset.18 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.socialInteractions | P | button | "P" | UI_ONLY |
| keybinds | keybinds..Reset.19 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.attack | Left Button | button | "Left Button" | UI_ONLY |
| keybinds | keybinds..Reset.20 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.pickItem | Middle Button | button | "Middle Button" | UI_ONLY |
| keybinds | keybinds..Reset.21 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.use | Right Button | button | "Right Button" | UI_ONLY |
| keybinds | keybinds..Reset.22 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.drop | Q | button | "Q" | UI_ONLY |
| keybinds | keybinds..Reset.23 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.1 | 1 | button | "1" | UI_ONLY |
| keybinds | keybinds..Reset.24 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.2 | 2 | button | "2" | UI_ONLY |
| keybinds | keybinds..Reset.25 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.3 | 3 | button | "3" | UI_ONLY |
| keybinds | keybinds..Reset.26 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.4 | 4 | button | "4" | UI_ONLY |
| keybinds | keybinds..Reset.27 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.5 | 5 | button | "5" | UI_ONLY |
| keybinds | keybinds..Reset.28 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.6 | 6 | button | "6" | UI_ONLY |
| keybinds | keybinds..Reset.29 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.7 | 7 | button | "7" | UI_ONLY |
| keybinds | keybinds..Reset.30 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.8 | 8 | button | "8" | UI_ONLY |
| keybinds | keybinds..Reset.31 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.hotbar.9 | 9 | button | "9" | UI_ONLY |
| keybinds | keybinds..Reset.32 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.inventory | E | button | "E" | UI_ONLY |
| keybinds | keybinds..Reset.33 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.swapOffhand | F | button | "F" | UI_ONLY |
| keybinds | keybinds..Reset.34 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.loadToolbarActivator | X | button | "X" | UI_ONLY |
| keybinds | keybinds..Reset.35 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.saveToolbarActivator | C | button | "C" | UI_ONLY |
| keybinds | keybinds..Reset.36 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.spectatorOutlines | Not Bound | button | "Not Bound" | UI_ONLY |
| keybinds | keybinds..Reset.37 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.spectatorHotbar | Middle Button | button | "Middle Button" | UI_ONLY |
| keybinds | keybinds..Reset.38 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.overlay | F3 | button | "F3" | UI_ONLY |
| keybinds | keybinds..Reset.39 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.modifier | F3 | button | "F3" | UI_ONLY |
| keybinds | keybinds..Reset.40 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.clearChat | D | button | "D" | UI_ONLY |
| keybinds | keybinds..Reset.41 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.copyRecreateCommand | I | button | "I" | UI_ONLY |
| keybinds | keybinds..Reset.42 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.copyLocation | C | button | "C" | UI_ONLY |
| keybinds | keybinds..Reset.43 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.spectate | N | button | "N" | UI_ONLY |
| keybinds | keybinds..Reset.44 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.crash | C | button | "C" | UI_ONLY |
| keybinds | keybinds..Reset.45 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.debugOptions | F6 | button | "F6" | UI_ONLY |
| keybinds | keybinds..Reset.46 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.dumpDynamicTextures | S | button | "S" | UI_ONLY |
| keybinds | keybinds..Reset.47 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.dumpVersion | V | button | "V" | UI_ONLY |
| keybinds | keybinds..Reset.48 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.switchGameMode | F4 | button | "F4" | UI_ONLY |
| keybinds | keybinds..Reset.49 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.reloadChunk | A | button | "A" | UI_ONLY |
| keybinds | keybinds..Reset.50 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.reloadResourcePacks | T | button | "T" | UI_ONLY |
| keybinds | keybinds..Reset.51 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.showAdvancedTooltips | H | button | "H" | UI_ONLY |
| keybinds | keybinds..Reset.52 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.showChunkBorders | G | button | "G" | UI_ONLY |
| keybinds | keybinds..Reset.53 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.showHitboxes | B | button | "B" | UI_ONLY |
| keybinds | keybinds..Reset.54 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.profiling | L | button | "L" | UI_ONLY |
| keybinds | keybinds..Reset.55 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.improvedTransparency | X | button | "X" | UI_ONLY |
| keybinds | keybinds..Reset.56 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.focusPause | P | button | "P" | UI_ONLY |
| keybinds | keybinds..Reset.57 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.profilingChart | 1 | button | "1" | UI_ONLY |
| keybinds | keybinds..Reset.58 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.fpsCharts | 2 | button | "2" | UI_ONLY |
| keybinds | keybinds..Reset.59 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.networkCharts | 3 | button | "3" | UI_ONLY |
| keybinds | keybinds..Reset.60 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.debug.lightmapTexture | 4 | button | "4" | UI_ONLY |
| keybinds | keybinds..Reset.61 | Reset | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| keybinds | keybinds..Reset_Keys | Reset Keys | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| worlds | worlds..search_for_worlds | search for worlds | text | "" | UI_ONLY |
| worlds | worlds.fictional-forest | Fictional Forest Camp | option | false | UI_ONLY |
| worlds | worlds.fictional-build | Fictional Creative Workshop | option | false | UI_ONLY |
| worlds | worlds..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| worlds | worlds..Play_Selected_World | Play Selected World | button | "Not applicable (command)" | UI_ONLY |
| worlds | worlds..Create_New_World | Create New World | button | "Not applicable (command)" | UI_ONLY |
| worlds | worlds..Edit | Edit | button | "Not applicable (command)" | UI_ONLY |
| worlds | worlds..Delete | Delete | button | "Not applicable (command)" | UI_ONLY |
| worlds | worlds..Re-Create | Re-Create | button | "Not applicable (command)" | UI_ONLY |
| worlds | worlds..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..Game | Game | tab | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..World | World | tab | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..More | More | tab | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..World_Name | World Name | text | "New World" | UI_ONLY |
| create/game | create.game.draft.gameMode | Game Mode: Survival | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game.draft.difficulty | Difficulty: Normal | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game.draft.commands | Allow Cheats: OFF | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..Create_New_World | Create New World | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..Game | Game | tab | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..World | World | tab | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..More | More | tab | "Not applicable (command)" | UI_ONLY |
| create/world | create.world.draft.type | World Type: Default | button | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..Customize | Customize | button | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..Seed_for_the_world_generator | Seed for the world generator | text | "" | UI_ONLY |
| create/world | create.world.switch.Generate_Structures | ON | button | "Not applicable (command)" | UI_ONLY |
| create/world | create.world.switch.Bonus_Chest | OFF | button | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..Create_New_World | Create New World | button | "Not applicable (command)" | UI_ONLY |
| create/world | create.world..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..Game | Game | tab | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..World | World | tab | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..More | More | tab | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..Game_Rules | Game Rules | button | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..Experiments | Experiments | button | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..Data_Packs | Data Packs | button | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..Create_New_World | Create New World | button | "Not applicable (command)" | UI_ONLY |
| create/more | create.more..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..World_Name | World Name | text | "New World" | UI_ONLY |
| editworld | editworld..Reset_Icon | Reset Icon | button | "Not applicable (command)" | INTENTIONALLY_DISABLED |
| editworld | editworld..Open_World_Folder | Open World Folder | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Make_Backup | Make Backup | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Open_Backups_Folder | Open Backups Folder | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Optimize_World | Optimize World | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld.world-save | Save | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| safety | safety..Do_not_show_this_screen_again | Do not show this screen again | checkbox | false | UI_ONLY |
| safety | safety..Proceed | Proceed | button | "Not applicable (command)" | UI_ONLY |
| safety | safety..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer.fictional-example | Fictional Example Server | option | false | UI_ONLY |
| multiplayer | multiplayer..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| multiplayer | multiplayer..Join_Server | Join Server | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer..Direct_Connection | Direct Connection | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer..Add_Server | Add Server | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer..Edit | Edit | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer..Delete | Delete | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer..Refresh | Refresh | button | "Not applicable (command)" | UI_ONLY |
| multiplayer | multiplayer..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| direct | direct..Server_Address | Server Address | text | "" | UI_ONLY |
| direct | direct.direct-connect | Join Server | button | "Not applicable (command)" | UI_ONLY |
| direct | direct..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| serveredit | serveredit..Server_Name | Server Name | text | "Minecraft Server" | UI_ONLY |
| serveredit | serveredit..Server_Address | Server Address | text | "" | UI_ONLY |
| serveredit | serveredit.server.pack | Server Resource Packs: Prompt | button | "Not applicable (command)" | UI_ONLY |
| serveredit | serveredit.server-save | Done | button | "Not applicable (command)" | UI_ONLY |
| serveredit | serveredit..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| realms | realms..Join_Realm | Join Realm | button | "Not applicable (command)" | UI_ONLY |
| realms | realms..Pending_Invites | Pending Invites | button | "Not applicable (command)" | UI_ONLY |
| realms | realms..Realms_news | Realms news | button | "Not applicable (command)" | UI_ONLY |
| realms | realms..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| realms | realms..Add_Realm | Add Realm | button | "Not applicable (command)" | UI_ONLY |
| realms | realms..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| realmjoin | realmjoin..Join_Code | Join Code | text | "" | UI_ONLY |
| realmjoin | realmjoin.realm-join | Join Realm | button | "Not applicable (command)" | UI_ONLY |
| realmjoin | realmjoin..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft | Programmer ArtThe classic look of Minecraft | option | false | UI_ONLY |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Select_Programmer_Art | Select Programmer Art | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft | High ContrastEnhances the UI contrast of Minecraft | option | false | UI_ONLY |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Move_High_Contrast_to_Available | Move High Contrast to Available | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Move_High_Contrast_up | Move High Contrast up | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Move_High_Contrast_down | Move High Contrast down | button | "Not applicable (command)" | UI_ONLY |
| packs | packs..Open_Pack_Folder | Open Pack Folder | button | "Not applicable (command)" | UI_ONLY |
| packs | packs..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| datapacks | datapacks..Open_Pack_Folder | Open Pack Folder | button | "Not applicable (command)" | UI_ONLY |
| datapacks | datapacks..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments.switch.Minecart_Improvements | OFF | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments.switch.Redstone_Experiments | OFF | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments.switch.Villager_Trade_Rebalance | OFF | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| experiments | experiments..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| rules | rules..Search... | Search... | text | "" | UI_ONLY |
| rules | rules.rule.command_block_output | ON | button | true | UI_ONLY |
| rules | rules.rule.log_admin_commands | ON | button | true | UI_ONLY |
| rules | rules.rule.send_command_feedback | ON | button | true | UI_ONLY |
| rules | rules.rule.show_advancement_messages | ON | button | true | UI_ONLY |
| rules | rules.rule.show_death_messages | ON | button | true | UI_ONLY |
| rules | rules.rule.block_drops | ON | button | true | UI_ONLY |
| rules | rules.rule.block_explosion_drop_decay | ON | button | true | UI_ONLY |
| rules | rules.rule.entity_drops | ON | button | true | UI_ONLY |
| rules | rules.rule.mob_drops | ON | button | true | UI_ONLY |
| rules | rules.rule.mob_explosion_drop_decay | ON | button | true | UI_ONLY |
| rules | rules.rule.projectiles_can_break_blocks | ON | button | true | UI_ONLY |
| rules | rules.rule.tnt_explosion_drop_decay | OFF | button | false | UI_ONLY |
| rules | rules.rule.allow_entering_nether_using_portals | ON | button | true | UI_ONLY |
| rules | rules.rule.command_blocks_work | ON | button | true | UI_ONLY |
| rules | rules.rule.global_sound_events | ON | button | true | UI_ONLY |
| rules | rules.max_block_modifications | Command modification block limit | text | 32768 | UI_ONLY |
| rules | rules.max_command_forks | Command context limit | text | 65536 | UI_ONLY |
| rules | rules.max_command_sequence_length | Command chain size limit | text | 65536 | UI_ONLY |
| rules | rules.max_minecart_speed | Minecart max speed | text | 8 | UI_ONLY |
| rules | rules.rule.reduced_debug_info | OFF | button | false | UI_ONLY |
| rules | rules.rule.spawner_blocks_work | ON | button | true | UI_ONLY |
| rules | rules.rule.tnt_explodes | ON | button | true | UI_ONLY |
| rules | rules.rule.forgive_dead_players | ON | button | true | UI_ONLY |
| rules | rules.max_entity_cramming | Entity cramming threshold | text | 24 | UI_ONLY |
| rules | rules.rule.mob_griefing | ON | button | true | UI_ONLY |
| rules | rules.rule.raids | ON | button | true | UI_ONLY |
| rules | rules.rule.universal_anger | OFF | button | false | UI_ONLY |
| rules | rules.rule.drowning_damage | ON | button | true | UI_ONLY |
| rules | rules.rule.elytra_movement_check | ON | button | true | UI_ONLY |
| rules | rules.rule.ender_pearls_vanish_on_death | ON | button | true | UI_ONLY |
| rules | rules.rule.fall_damage | ON | button | true | UI_ONLY |
| rules | rules.rule.fire_damage | ON | button | true | UI_ONLY |
| rules | rules.rule.freeze_damage | ON | button | true | UI_ONLY |
| rules | rules.rule.immediate_respawn | OFF | button | false | UI_ONLY |
| rules | rules.rule.keep_inventory | OFF | button | false | UI_ONLY |
| rules | rules.rule.limited_crafting | OFF | button | false | UI_ONLY |
| rules | rules.rule.locator_bar | ON | button | true | UI_ONLY |
| rules | rules.rule.natural_health_regeneration | ON | button | true | UI_ONLY |
| rules | rules.rule.player_movement_check | ON | button | true | UI_ONLY |
| rules | rules.players_nether_portal_creative_delay | Player's Nether Portal delay in creative mode | text | 0 | UI_ONLY |
| rules | rules.players_nether_portal_default_delay | Player's Nether Portal delay in non-creative mode | text | 80 | UI_ONLY |
| rules | rules.players_sleeping_percentage | Sleep percentage | text | 100 | UI_ONLY |
| rules | rules.rule.pvp | ON | button | true | UI_ONLY |
| rules | rules.respawn_radius | Respawn location radius | text | 10 | UI_ONLY |
| rules | rules.rule.spectators_generate_chunks | ON | button | true | UI_ONLY |
| rules | rules.rule.spawn_mobs | ON | button | true | UI_ONLY |
| rules | rules.rule.spawn_monsters | ON | button | true | UI_ONLY |
| rules | rules.rule.spawn_patrols | ON | button | true | UI_ONLY |
| rules | rules.rule.spawn_phantoms | ON | button | true | UI_ONLY |
| rules | rules.rule.spawn_wandering_traders | ON | button | true | UI_ONLY |
| rules | rules.rule.spawn_wardens | ON | button | true | UI_ONLY |
| rules | rules.rule.advance_time | ON | button | true | UI_ONLY |
| rules | rules.rule.advance_weather | ON | button | true | UI_ONLY |
| rules | rules.fire_spread_radius_around_player | Fire spread radius | text | 128 | UI_ONLY |
| rules | rules.rule.lava_source_conversion | OFF | button | false | UI_ONLY |
| rules | rules.max_snow_accumulation_height | Snow accumulation height | text | 1 | UI_ONLY |
| rules | rules.random_tick_speed | Random tick speed rate | text | 3 | UI_ONLY |
| rules | rules.rule.spread_vines | ON | button | true | UI_ONLY |
| rules | rules.rule.water_source_conversion | ON | button | true | UI_ONLY |
| rules | rules..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| rules | rules.rules-done | Done | button | "Not applicable (command)" | UI_ONLY |
| rules | rules..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| flat | flat.Grass_Block1 | Grass Block1 | option | null | UI_ONLY |
| flat | flat.Dirt2 | Dirt2 | option | null | UI_ONLY |
| flat | flat.Bedrock1 | Bedrock1 | option | null | UI_ONLY |
| flat | flat..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| flat | flat..Remove_Layer | Remove Layer | button | "Not applicable (command)" | UI_ONLY |
| flat | flat..Presets | Presets | button | "Not applicable (command)" | UI_ONLY |
| flat | flat..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| flat | flat..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| presets | presets..Want_to_share_your_preset_with_someone__Use_the_box_below_ | Want to share your preset with someone? Use the box below! | text | "minecraft:bedrock,2*minecraft:dirt,minecraft:grass_block;minecraft:plains" | UI_ONLY |
| presets | presets.classic_flat | Classic Flat | option | false | UI_ONLY |
| presets | presets.tunnelers_dream | Tunnelers' Dream | option | false | UI_ONLY |
| presets | presets.water_world | Water World | option | false | UI_ONLY |
| presets | presets.overworld | Overworld | option | false | UI_ONLY |
| presets | presets.snowy_kingdom | Snowy Kingdom | option | false | UI_ONLY |
| presets | presets.bottomless_pit | Bottomless Pit | option | false | UI_ONLY |
| presets | presets.desert | Desert | option | false | UI_ONLY |
| presets | presets.redstone_ready | Redstone Ready | option | false | UI_ONLY |
| presets | presets.the_void | The Void | option | false | UI_ONLY |
| presets | presets..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| presets | presets..Use_Preset | Use Preset | button | "Not applicable (command)" | UI_ONLY |
| presets | presets..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| biomes | biomes..Search... | Search... | text | "" | UI_ONLY |
| biomes | biomes.Badlands | Badlands | option | "false" | UI_ONLY |
| biomes | biomes.Bamboo_Jungle | Bamboo Jungle | option | "false" | UI_ONLY |
| biomes | biomes.Basalt_Deltas | Basalt Deltas | option | "false" | UI_ONLY |
| biomes | biomes.Beach | Beach | option | "false" | UI_ONLY |
| biomes | biomes.Birch_Forest | Birch Forest | option | "false" | UI_ONLY |
| biomes | biomes.Cherry_Grove | Cherry Grove | option | "false" | UI_ONLY |
| biomes | biomes.Cold_Ocean | Cold Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Crimson_Forest | Crimson Forest | option | "false" | UI_ONLY |
| biomes | biomes.Dappled_Forest | Dappled Forest | option | "false" | UI_ONLY |
| biomes | biomes.Dark_Forest | Dark Forest | option | "false" | UI_ONLY |
| biomes | biomes.Deep_Cold_Ocean | Deep Cold Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Deep_Dark | Deep Dark | option | "false" | UI_ONLY |
| biomes | biomes.Deep_Frozen_Ocean | Deep Frozen Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Deep_Lukewarm_Ocean | Deep Lukewarm Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Deep_Ocean | Deep Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Desert | Desert | option | "false" | UI_ONLY |
| biomes | biomes.Dripstone_Caves | Dripstone Caves | option | "false" | UI_ONLY |
| biomes | biomes.End_Barrens | End Barrens | option | "false" | UI_ONLY |
| biomes | biomes.End_Highlands | End Highlands | option | "false" | UI_ONLY |
| biomes | biomes.End_Midlands | End Midlands | option | "false" | UI_ONLY |
| biomes | biomes.Eroded_Badlands | Eroded Badlands | option | "false" | UI_ONLY |
| biomes | biomes.Flower_Forest | Flower Forest | option | "false" | UI_ONLY |
| biomes | biomes.Forest | Forest | option | "false" | UI_ONLY |
| biomes | biomes.Frozen_Ocean | Frozen Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Frozen_Peaks | Frozen Peaks | option | "false" | UI_ONLY |
| biomes | biomes.Frozen_River | Frozen River | option | "false" | UI_ONLY |
| biomes | biomes.Grove | Grove | option | "false" | UI_ONLY |
| biomes | biomes.Ice_Spikes | Ice Spikes | option | "false" | UI_ONLY |
| biomes | biomes.Jagged_Peaks | Jagged Peaks | option | "false" | UI_ONLY |
| biomes | biomes.Jungle | Jungle | option | "false" | UI_ONLY |
| biomes | biomes.Lukewarm_Ocean | Lukewarm Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Lush_Caves | Lush Caves | option | "false" | UI_ONLY |
| biomes | biomes.Mangrove_Swamp | Mangrove Swamp | option | "false" | UI_ONLY |
| biomes | biomes.Meadow | Meadow | option | "false" | UI_ONLY |
| biomes | biomes.Mushroom_Fields | Mushroom Fields | option | "false" | UI_ONLY |
| biomes | biomes.Nether_Wastes | Nether Wastes | option | "false" | UI_ONLY |
| biomes | biomes.Ocean | Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Old_Growth_Birch_Forest | Old Growth Birch Forest | option | "false" | UI_ONLY |
| biomes | biomes.Old_Growth_Pine_Taiga | Old Growth Pine Taiga | option | "false" | UI_ONLY |
| biomes | biomes.Old_Growth_Spruce_Taiga | Old Growth Spruce Taiga | option | "false" | UI_ONLY |
| biomes | biomes.Pale_Garden | Pale Garden | option | "false" | UI_ONLY |
| biomes | biomes.Plains | Plains | option | "true" | UI_ONLY |
| biomes | biomes.River | River | option | "false" | UI_ONLY |
| biomes | biomes.Savanna | Savanna | option | "false" | UI_ONLY |
| biomes | biomes.Savanna_Plateau | Savanna Plateau | option | "false" | UI_ONLY |
| biomes | biomes.Small_End_Islands | Small End Islands | option | "false" | UI_ONLY |
| biomes | biomes.Snowy_Beach | Snowy Beach | option | "false" | UI_ONLY |
| biomes | biomes.Snowy_Plains | Snowy Plains | option | "false" | UI_ONLY |
| biomes | biomes.Snowy_Slopes | Snowy Slopes | option | "false" | UI_ONLY |
| biomes | biomes.Snowy_Taiga | Snowy Taiga | option | "false" | UI_ONLY |
| biomes | biomes.Soul_Sand_Valley | Soul Sand Valley | option | "false" | UI_ONLY |
| biomes | biomes.Sparse_Jungle | Sparse Jungle | option | "false" | UI_ONLY |
| biomes | biomes.Stony_Peaks | Stony Peaks | option | "false" | UI_ONLY |
| biomes | biomes.Stony_Shore | Stony Shore | option | "false" | UI_ONLY |
| biomes | biomes.Sulfur_Caves | Sulfur Caves | option | "false" | UI_ONLY |
| biomes | biomes.Sunflower_Plains | Sunflower Plains | option | "false" | UI_ONLY |
| biomes | biomes.Swamp | Swamp | option | "false" | UI_ONLY |
| biomes | biomes.Taiga | Taiga | option | "false" | UI_ONLY |
| biomes | biomes.The_End | The End | option | "false" | UI_ONLY |
| biomes | biomes.The_Void | The Void | option | "false" | UI_ONLY |
| biomes | biomes.Warm_Ocean | Warm Ocean | option | "false" | UI_ONLY |
| biomes | biomes.Warped_Forest | Warped Forest | option | "false" | UI_ONLY |
| biomes | biomes.Windswept_Forest | Windswept Forest | option | "false" | UI_ONLY |
| biomes | biomes.Windswept_Gravelly_Hills | Windswept Gravelly Hills | option | "false" | UI_ONLY |
| biomes | biomes.Windswept_Hills | Windswept Hills | option | "false" | UI_ONLY |
| biomes | biomes.Windswept_Savanna | Windswept Savanna | option | "false" | UI_ONLY |
| biomes | biomes.Wooded_Badlands | Wooded Badlands | option | "false" | UI_ONLY |
| biomes | biomes..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| biomes | biomes..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| biomes | biomes..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Credits | Credits | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Attribution | Attribution | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Licenses | Licenses | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Privacy_Statement | Privacy Statement | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Give_Feedback | Give Feedback | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| telemetry | telemetry..I_consent_to_sending_optional_telemetry_data | I consent to sending optional telemetry data | checkbox | false | UI_ONLY |
| telemetry | telemetry..View_My_Data | View My Data | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| linkconfirm | linkconfirm..Open_in_Browser | Open in Browser | button | "Not applicable (command)" | UI_ONLY |
| linkconfirm | linkconfirm..Copy_to_Clipboard | Copy to Clipboard | button | "Not applicable (command)" | UI_ONLY |
| linkconfirm | linkconfirm..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| message | message..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Proceed | Proceed | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| quit | quit..Return_to_Title | Return to Title | button | "Not applicable (command)" | UI_ONLY |
| skin | skin.skin_cape | Cape: ON | button | true | UI_ONLY |
| skin | skin.skin_jacket | Jacket: ON | button | true | UI_ONLY |
| skin | skin.skin_left_sleeve | Left Sleeve: ON | button | true | UI_ONLY |
| skin | skin.skin_right_sleeve | Right Sleeve: ON | button | true | UI_ONLY |
| skin | skin.skin_left_pants_leg | Left Pant Leg: ON | button | true | UI_ONLY |
| skin | skin.skin_right_pants_leg | Right Pant Leg: ON | button | true | UI_ONLY |
| skin | skin.skin_hat | Hat: ON | button | true | UI_ONLY |
| skin | skin.mainHand | Main Hand: Right | button | "Right" | UI_ONLY |
| skin | skin..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| skin | skin..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| chat | chat.chatVisibility | Chat: Shown | button | "Shown" | UI_ONLY |
| chat | chat.chatColors | Colors: ON | button | true | UI_ONLY |
| chat | chat.chatLinks | Web Links: ON | button | true | UI_ONLY |
| chat | chat.chatLinksPrompt | Prompt on Links: ON | button | true | UI_ONLY |
| chat | chat.chatOpacity | Chat Text Opacity | range | 1 | UI_ONLY |
| chat | chat.textBackgroundOpacity | Text Background Opacity | range | 0.5 | UI_ONLY |
| chat | chat.chatScale | Chat Text Size | range | 1 | UI_ONLY |
| chat | chat.chatLineSpacing | Line Spacing | range | 0 | UI_ONLY |
| chat | chat.chatDelay | options.chat.delay_instant | range | 0 | UI_ONLY |
| chat | chat.chatWidth | Width | range | 1 | UI_ONLY |
| chat | chat.chatHeightFocused | Focused Height | range | 1 | UI_ONLY |
| chat | chat.chatHeightUnfocused | Unfocused Height | range | 0.44366197 | UI_ONLY |
| chat | chat.narrator | Narrator: Narrates All | button | "OFF" | UI_ONLY |
| chat | chat.autoSuggestions | Command Suggestions: ON | button | true | UI_ONLY |
| chat | chat.hideMatchedNames | Hide Matched Names: ON | button | true | UI_ONLY |
| chat | chat.reducedDebugInfo | Reduced Debug Info: OFF | button | false | UI_ONLY |
| chat | chat.onlyShowSecureChat | Only Show Secure Chat: OFF | button | false | UI_ONLY |
| chat | chat.saveChatDrafts | Save Unsent Chats: OFF | button | false | UI_ONLY |
| chat | chat..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| chat | chat..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| mouse | mouse.sensitivity | Sensitivity | range | 0.5 | UI_ONLY |
| mouse | mouse.mouseWheelSensitivity | Scroll Sensitivity | range | 0 | UI_ONLY |
| mouse | mouse.discreteMouseScroll | Discrete Scrolling: OFF | button | false | UI_ONLY |
| mouse | mouse.invertXMouse | Invert Mouse X: OFF | button | false | UI_ONLY |
| mouse | mouse.invertYMouse | Invert Mouse Y: OFF | button | false | UI_ONLY |
| mouse | mouse.allowCursorChanges | Allow Cursor Changes: ON | button | true | UI_ONLY |
| mouse | mouse..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| mouse | mouse..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| font | font.forceUnicodeFont | Force Unicode Font: OFF | button | false | UI_ONLY |
| font | font.japaneseGlyphVariants | Japanese Glyph Variants: OFF | button | false | UI_ONLY |
| font | font..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| font | font..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Delete | Delete | button | "Not applicable (command)" | UI_ONLY |
| message | message..Restore_Entry | Restore Entry | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Turn_ON | Turn ON | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Turn_OFF | Turn OFF | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Move_Programmer_Art_to_Available | Move Programmer Art to Available | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Move_Programmer_Art_up | Move Programmer Art up | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Move_Programmer_Art_down | Move Programmer Art down | button | "Not applicable (command)" | UI_ONLY |
