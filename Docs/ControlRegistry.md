# Control Registry

This generated table is the earlier menu-settings snapshot. The newer Singleplayer routing is documented in [SuperFlat.md](SuperFlat.md): Super Flat is playable; the original two worlds remain references. Concurrent FPS changes have their own evidence under `Foundation/` and are not retroactively certified by this table.

Generated from rendered controls and structured definitions. Five video settings, confirmed preference reset and normal Quit Game are implemented. Seven movement binding settings and two Hold/Toggle policies use native Enhanced Input; actual character gameplay remains pending. Other binding rows are Planned and disabled. See Keybindings.md. See VideoSettings.md and QuitGame.md for functional acceptance and remaining human gates. `ControlRegistry.json` records the status of every entry.

Observed 697 distinct controls across 33 source routes and Create tabs. Dynamic selected-pack controls are included in the supplemental observations.

101 additional scalar preferences, language, pack order, warning dismissal and telemetry choice now use native persistence. Saved choices and implemented effects have separate statuses. See MenuPreferences.md.

| Screen | Stable ID | Exact observed label | Widget | Default | Status |
|---|---|---|---|---|---|
| main | main..Singleplayer | Singleplayer | button | "Not applicable (command)" | UI_ONLY |
| main | main..Multiplayer | Multiplayer | button | "Not applicable (command)" | UI_ONLY |
| main | main..Friends_List | Friends List | button | "Not applicable (command)" | UI_ONLY |
| main | main..Language | Language | button | "" | UI_ONLY |
| main | main..Accessibility_Settings | Accessibility Settings | button | "Not applicable (command)" | UI_ONLY |
| main | main..Options... | Options... | button | "Not applicable (command)" | UI_ONLY |
| main | main..Quit_Game | Quit Game | button | "Not applicable (command)" | IMPLEMENTED |
| main | main..Copyright_Mojang_AB._Do_not_distribute_ | Copyright Mojang AB. Do not distribute! | button | "Not applicable (command)" | UI_ONLY |
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
| safety | safety..Do_not_show_this_screen_again | Do not show this screen again | checkbox | false | PREFERENCE_IMPLEMENTED |
| safety | safety..Proceed | Proceed | button | "Not applicable (command)" | UI_ONLY |
| safety | safety..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| message | message..Back | Back | button | "Not applicable (command)" | UI_ONLY |
| language | language..Language | Language | text | "" | UI_ONLY |
| language | language.af_za | Afrikaans (Suid-Afrika) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ar_sa | العربية (العالم العربي) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ast_es | Asturianu (Asturies) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.az_az | Azərbaycanca (Azərbaycan) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ba_ru | Башҡортса (Башҡортостан, Рәсәй) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.bar | Boarisch (Bayern) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.be_by | Беларуская (Беларусь) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.be_latn | Biełaruskaja (Biełaruś) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.bg_bg | Български (България) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.br_fr | Brezhoneg (Breizh) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.brb | Braobans (Braobant) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.bs_ba | Bosanski (Bosna i Hercegovina) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ca_es | Català (Catalunya) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.cs_cz | Čeština (Česko) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.cv_cu | Чӑвашла (Чӑваш Ен, Раҫҫей) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.cy_gb | Cymraeg (Cymru) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.da_dk | Dansk (Danmark) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.de_at | Deitsch (Österreich) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.de_ch | Schwiizerdütsch (Schwiiz) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.de_de | Deutsch (Deutschland) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.el_gr | Ελληνικά (Ελλάδα) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_au | English (Australia) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_ca | English (Canada) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_gb | English (United Kingdom) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_nz | English (New Zealand) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_pt | Pirate Speak (The Seven Seas) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_ud | ɥsᴉꞁᵷuƎ (uʍoᗡ ǝpᴉsd∩) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.en_us | English (US) | option | "true" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.enp | Anglish (Oned Riches) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.enws | Shakespearean English (Kingdom of England) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.eo_uy | Esperanto (Esperantujo) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_ar | Español (Argentina) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_cl | Español (Chile) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_ec | Español (Ecuador) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_es | Español (España) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_mx | Español (México) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_uy | Español (Uruguay) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.es_ve | Español (Venezuela) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.esan | Andalûh (Andaluçía) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.et_ee | Eesti keel (Eesti) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.eu_es | Euskara (Euskal Herria) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fa_ir | فارسی (ايران) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fi_fi | Suomi (Suomi) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fil_ph | Filipino (Pilipinas) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fo_fo | Føroyskt (Føroyar) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fr_ca | Français (Canada) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fr_ch | Français (Suisse) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fr_fr | Français (France) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fra_de | Fränggisch (Franggn) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fur_it | Furlan (Friûl) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.fy_nl | Frysk (Fryslân) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ga_ie | Gaeilge (Éire) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.gd_gb | Gàidhlig (Alba) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.gl_es | Galego (Galicia / Galiza) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.go_fr | Galo (Bertègn) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.got_de | 𐌲𐌿𐍄𐍂𐌰𐌶𐌳𐌰 (𐌲𐌿𐍄𐌸𐌹𐌿𐌳𐌰) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.hal_ua | Галицка (Галичина, Вкраїна) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.haw_us | ʻŌlelo Hawaiʻi (Hawaiʻi) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.he_il | עברית (ישראל) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.hi_in | हिंदी (भारत) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.hn_no | Høgnorsk (Norig) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.hr_hr | Hrvatski (Hrvatska) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.hu_hu | Magyar (Magyarország) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.hy_am | Հայերեն (Հայաստան) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.id_id | Bahasa Indonesia (Indonesia) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ig_ng | Igbo (Naigeria) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.io_en | Ido (Idia) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.is_is | Íslenska (Ísland) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.isv | Medžuslovjansky (Slovjanščina) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.it_it | Italiano (Italia) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ja_jp | 日本語 (日本) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.jbo_en | la .lojban. (la jbogu'e) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ka_ge | ქართული (საქართველო) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.kk_kz | Қазақша (Қазақстан) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.kn_in | ಕನ್ನಡ (ಭಾರತ) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ko_kr | 한국어 (대한민국) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ksh | Kölsch/Ripoarisch (Rhingland) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.kw_gb | Kernewek (Kernow) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ky_kg | Кыргызча (Кыргызстан) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.la_la | Latina (Latium) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lb_lu | Lëtzebuergesch (Lëtzebuerg) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.li_li | Limburgs (Limburg) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lmo | Lombard (Lombardia) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lo_la | ລາວ (ປະເທດລາວ) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lol_us | LOLCAT (Kingdom of Cats) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lt_lt | Lietuvių (Lietuva) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lv_lv | Latviešu (Latvija) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.lzh | 文言 (華夏) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.mk_mk | Македонски (Северна Македонија) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.mn_mn | Монгол (Монгол Улс) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ms_my | Bahasa Melayu (Malaysia) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.mt_mt | Malti (Malta) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.nah | Mēxikatlahtōlli (Mēxiko) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.nds_de | Plattdüütsch (Düütschland) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.nl_be | Vlaams (België) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.nl_nl | Nederlands (Nederland) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.nn_no | Norsk nynorsk (Noreg) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.no_no | Norsk bokmål (Norge) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.oc_fr | Occitan (Occitània) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ovd | Övdalska (Swerre) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.pl_pl | Polski (Polska) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.pls | Ngiiwà (Ndanìꞌngà) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.pt_br | Português (Brasil) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.pt_pt | Português (Portugal) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.qcb_es | Cántabru/Montañés (Cantabria) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.qid | Bhs. Indonesia edjaän lama (Indonesia tempo doeloe) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.qya_aa | Quenya (Arda) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ro_ro | Română (România) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.rpr | Русскій дореформенный (Россійская имперія) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ru_ru | Русский (Россия) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ry_ua | Руснацькый (Пудкарпатя, Украина) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sah_sah | Сахалыы (Cаха Сирэ) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.se_no | Davvisámegiella (Sápmi) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sk_sk | Slovenčina (Slovensko) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sl_si | Slovenščina (Slovenija) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.so_so | Soomaali (Soomaaliya) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sq_al | Shqip (Shqipëri) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sr_cs | Srpski (Srbija) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sr_sp | Српски (Србија) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sv_se | Svenska (Sverige) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.sxu | Säggs’sch (Saggsn) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.szl | Ślōnski (Gōrny Ślōnsk) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.ta_in | தமிழ் (இந்தியா) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.th_th | ไทย (ประเทศไทย) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.tl_ph | Tagalog (Pilipinas) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.tlh_aa | tlhIngan Hol (tlhIngan wo') | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.tok | toki pona (kulupu pona) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.tr_tr | Türkçe (Türkiye) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.tt_ru | Татарча (Татарстан, Рәсәй) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.tzo_mx | Bats'i k'op (Jobel) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.uk_ua | Українська (Україна) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.uz_uz | O'zbekcha (O'zbekiston) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.val_es | Català (Valencià) (País Valencià) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.vec_it | Vèneto (Veneto) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.vi_vn | Tiếng Việt (Việt Nam) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.vp_vl | Viossa (Vilant) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.vro | Võro (Eesti) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.yi_de | ייִדיש (אשכנזיש יידן) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.yo_ng | Yorùbá (Nàìjíríà) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.zh_cn | 简体中文 (中国大陆) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.zh_hk | 繁體中文 (香港特別行政區) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.zh_tw | 繁體中文 (台灣) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language.zlm_arab | بهاس ملايو (مليسيا) | option | "false" | PREFERENCE_SAVED_EFFECT_PENDING |
| language | language..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| language | language..Font_Settings... | Font Settings... | button | "Not applicable (command)" | UI_ONLY |
| language | language..Done | Done | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.narrator | Narrator: Narrates All | button | "OFF" | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility..Controls... | Controls... | button | "Not applicable (command)" | UI_ONLY |
| accessibility | accessibility.showSubtitles | Closed Captions: OFF | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.highContrast | High Contrast: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.menuBackgroundBlurriness | Menu Background Blur | range | 5 | IMPLEMENTED |
| accessibility | accessibility.textBackgroundOpacity | Text Background Opacity | range | 0.5 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.backgroundForChatOnly | Everywhere | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.chatOpacity | Chat Text Opacity | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.chatLineSpacing | Line Spacing | range | 0 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.chatDelay | options.chat.delay_instant | range | 0 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.notificationDisplayTime | Notification Time | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.bobView | View Bobbing: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.screenEffectScale | Distortion Effects | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.fovEffectScale | FOV Effects | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.darknessEffectScale | Darkness Pulsing | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.damageTiltStrength | Damage Tilt | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.glintSpeed | Glint Speed | range | 0.5 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.glintStrength | Glint Strength | range | 0.75 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.hideLightningFlash | Hide Sky Flashes: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.darkMojangStudiosBackground | Monochrome Logo: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.panoramaSpeed | Panorama Scroll Speed | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.hideSplashTexts | Hide Splash Texts: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.narratorHotkey | Narrator Hotkey: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility.rotateWithMinecart | Rotate with Minecarts: OFF | button | false | INTENTIONALLY_DISABLED |
| accessibility | accessibility.highContrastBlockOutline | High Contrast Block Outlines: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| accessibility | accessibility..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| accessibility | accessibility..Accessibility_Guide | Accessibility Guide | button | "Not applicable (command)" | UI_ONLY |
| accessibility | accessibility..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| options | options.fov | FOV | range | 70 | PREFERENCE_SAVED_EFFECT_PENDING |
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
| credits | credits..Credits | Credits | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Attribution | Attribution | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Licenses | Licenses | button | "Not applicable (command)" | UI_ONLY |
| credits | credits..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| online | online.friendsList | Friends List: OFF | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| online | online.allowFriendRequests | Allow Requests: OFF | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| online | online.inGameNotification | In-Game Notification: OFF | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| online | online.sharePresence | Visibility: Full | button | "Full" | PREFERENCE_SAVED_EFFECT_PENDING |
| online | online..Xbox_Settings... | Xbox Settings... | button | "Not applicable (command)" | UI_ONLY |
| online | online.allowServerListing | Allow Server Listings: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| online | online..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| online | online..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| skin | skin.skin_cape | Cape: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.skin_jacket | Jacket: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.skin_left_sleeve | Left Sleeve: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.skin_right_sleeve | Right Sleeve: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.skin_left_pants_leg | Left Pant Leg: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.skin_right_pants_leg | Right Pant Leg: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.skin_hat | Hat: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin.mainHand | Main Hand: Left | button | "Right" | PREFERENCE_SAVED_EFFECT_PENDING |
| skin | skin..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| skin | skin..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| sounds | sounds.sound_master | Master Volume | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_music | Music | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_record | Jukebox/Note Blocks | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_weather | Weather | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_block | Blocks | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_hostile | Hostile Mobs | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_neutral | Friendly Mobs | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_player | Players | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_ambient | Ambient/Environment | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_voice | Narrator/Voice | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.sound_ui | UI | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.soundDevice | System Default | button | "System Default" | UI_ONLY |
| sounds | sounds.showSubtitles | Closed Captions: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.directionalAudio | Directional Audio: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.musicFrequency | Music Frequency: Frequent | button | "Default" | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds.musicToast | Music Toast: Pause Menu | button | "Never" | PREFERENCE_SAVED_EFFECT_PENDING |
| sounds | sounds..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| sounds | sounds..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| video | video..Exclusive_Fullscreen_Mode | Exclusive Fullscreen Mode | range | "0" | INTENTIONALLY_DISABLED |
| video | video.framerateLimit | Max Framerate | range | 120 | IMPLEMENTED |
| video | video.enableVsync | VSync: OFF | button | true | IMPLEMENTED |
| video | video.inactivityFpsLimit | Reduce fps when: Minimized | button | "AFK" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.guiScale | GUI Scale: 1 | button | 0 | IMPLEMENTED |
| video | video.fullscreen | Fullscreen: ON | button | false | IMPLEMENTED |
| video | video.exclusiveFullscreen | Exclusive Fullscreen: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.gamma | Brightness | range | 0.5 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.preferredGraphicsBackend | Graphics API: DirectX 12 | button | "Default (DirectX 12)" | NATIVE_READ_ONLY |
| video | video.graphicsPreset | Preset | range | "Fancy" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.biomeBlendRadius | Biome Blend | range | 2 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.renderDistance | Render Distance | range | 12 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.prioritizeChunkUpdates | Chunk Builder: Semi Blocking | button | "Threaded" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.simulationDistance | Simulation Distance | range | 12 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.ambientOcclusion | Smooth Lighting: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.cloudStatus | Clouds: OFF | button | "Fancy" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.particles | Particles: Decreased | button | "All" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.mipmapLevels | Mipmap Levels | range | 4 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.entityShadows | Entity Shadows: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.entityDistanceScaling | Entity Distance | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.menuBackgroundBlurriness | Menu Background Blur | range | 5 | IMPLEMENTED |
| video | video.cloudRange | Cloud Distance | range | 128 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.cutoutLeaves | See-Through Leaves: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.improvedTransparency | Improved Transparency: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.textureFiltering | Texture Filtering: RGSS | button | "None" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.maxAnisotropyBit | Anisotropic Filtering | range | 2 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.weatherRadius | Weather Effect Radius | range | 10 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.showAutosaveIndicator | Autosave Indicator: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.vignette | Show Vignette: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.attackIndicator | Attack Indicator: Hotbar | button | "Crosshair" | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.chunkSectionFadeInTime | Chunk Fade Time | range | 0.75 | PREFERENCE_SAVED_EFFECT_PENDING |
| video | video.hard-reset | Hard Reset | button | "Not applicable (command)" | IMPLEMENTED |
| video | video..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| video | video..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| controls | controls..Mouse_Settings... | Mouse Settings... | button | "Not applicable (command)" | UI_ONLY |
| controls | controls..Key_Binds... | Key Binds... | button | "Not applicable (command)" | UI_ONLY |
| controls | controls.toggleCrouch | Crouch: Toggle | button | false | INPUT_SETTINGS_IMPLEMENTED |
| controls | controls.toggleSprint | Sprint: Toggle | button | false | INPUT_SETTINGS_IMPLEMENTED |
| controls | controls.toggleAttack | Attack/Destroy: Toggle | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| controls | controls.toggleUse | Use Item/Place Block: Toggle | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| controls | controls.autoJump | Auto-Jump: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| controls | controls.sprintWindow | Sprint Window | range | 7 | PREFERENCE_SAVED_EFFECT_PENDING |
| controls | controls.quitShortcuts | Quit Shortcuts: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| controls | controls.operatorItemsTab | Operator Items Tab: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| controls | controls..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| controls | controls..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| chat | chat.chatVisibility | Chat: Commands Only | button | "Shown" | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatColors | Colors: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatLinks | Web Links: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatLinksPrompt | Prompt on Links: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatOpacity | Chat Text Opacity | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.textBackgroundOpacity | Text Background Opacity | range | 0.5 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatScale | Chat Text Size | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatLineSpacing | Line Spacing | range | 0 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatDelay | options.chat.delay_instant | range | 0 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatWidth | Width | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatHeightFocused | Focused Height | range | 1 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.chatHeightUnfocused | Unfocused Height | range | 0.44366197 | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.narrator | Narrator: Narrates Chat | button | "OFF" | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.autoSuggestions | Command Suggestions: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.hideMatchedNames | Hide Matched Names: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.reducedDebugInfo | Reduced Debug Info: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.onlyShowSecureChat | Only Show Secure Chat: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat.saveChatDrafts | Save Unsent Chats: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| chat | chat..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| chat | chat..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft | Programmer ArtThe classic look of Minecraft | option | false | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Select_Programmer_Art | Select Programmer Art | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft | High ContrastEnhances the UI contrast of Minecraft | option | false | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Select_High_Contrast | Select High Contrast | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs..Open_Pack_Folder | Open Pack Folder | button | "Not applicable (command)" | UI_ONLY |
| packs | packs..Done | Done | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| telemetry | telemetry..Privacy_Statement | Privacy Statement | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Give_Feedback | Give Feedback | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| telemetry | telemetry..I_consent_to_sending_optional_telemetry_data | I consent to sending optional telemetry data | checkbox | false | PREFERENCE_SAVED_EFFECT_PENDING |
| telemetry | telemetry..View_My_Data | View My Data | button | "Not applicable (command)" | UI_ONLY |
| telemetry | telemetry..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Hard_Reset | Hard Reset | button | "Not applicable (command)" | IMPLEMENTED |
| confirm | confirm..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| mouse | mouse.sensitivity | Sensitivity | range | 0.5 | PREFERENCE_SAVED_EFFECT_PENDING |
| mouse | mouse.mouseWheelSensitivity | Scroll Sensitivity | range | 0 | PREFERENCE_SAVED_EFFECT_PENDING |
| mouse | mouse.discreteMouseScroll | Discrete Scrolling: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| mouse | mouse.invertXMouse | Invert Mouse X: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| mouse | mouse.invertYMouse | Invert Mouse Y: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| mouse | mouse.allowCursorChanges | Allow Cursor Changes: OFF | button | true | PREFERENCE_SAVED_EFFECT_PENDING |
| mouse | mouse..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| mouse | mouse..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| keybinds | keybinds.key.sneak | C | button | "C" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset | Reset | button | "C" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.jump | Space | button | "Space" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset.1 | Reset | button | "Space" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.sprint | Left Shift | button | "Left Shift" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset.2 | Reset | button | "Left Shift" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.left | A | button | "A" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset.3 | Reset | button | "A" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.right | D | button | "D" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset.4 | Reset | button | "D" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.back | S | button | "S" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset.5 | Reset | button | "S" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.forward | W | button | "W" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Reset.6 | Reset | button | "W" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds.key.advancements | L | button | "L" | PLANNED |
| keybinds | keybinds..Reset.7 | Reset | button | "L" | PLANNED |
| keybinds | keybinds.key.quickActions | G | button | "G" | PLANNED |
| keybinds | keybinds..Reset.8 | Reset | button | "G" | PLANNED |
| keybinds | keybinds.key.screenshot | F2 | button | "F2" | PLANNED |
| keybinds | keybinds..Reset.9 | Reset | button | "F2" | PLANNED |
| keybinds | keybinds.key.smoothCamera | Not Bound | button | "Not Bound" | PLANNED |
| keybinds | keybinds..Reset.10 | Reset | button | "Not Bound" | PLANNED |
| keybinds | keybinds.key.fullscreen | F11 | button | "F11" | PLANNED |
| keybinds | keybinds..Reset.11 | Reset | button | "F11" | PLANNED |
| keybinds | keybinds.key.toggleGui | F1 | button | "F1" | PLANNED |
| keybinds | keybinds..Reset.12 | Reset | button | "F1" | PLANNED |
| keybinds | keybinds.key.togglePerspective | F5 | button | "F5" | PLANNED |
| keybinds | keybinds..Reset.13 | Reset | button | "F5" | PLANNED |
| keybinds | keybinds.key.toggleSpectatorShaderEffects | F4 | button | "F4" | PLANNED |
| keybinds | keybinds..Reset.14 | Reset | button | "F4" | PLANNED |
| keybinds | keybinds.key.friends | O | button | "O" | PLANNED |
| keybinds | keybinds..Reset.15 | Reset | button | "O" | PLANNED |
| keybinds | keybinds.key.playerlist | Tab | button | "Tab" | PLANNED |
| keybinds | keybinds..Reset.16 | Reset | button | "Tab" | PLANNED |
| keybinds | keybinds.key.chat | T | button | "T" | PLANNED |
| keybinds | keybinds..Reset.17 | Reset | button | "T" | PLANNED |
| keybinds | keybinds.key.command | / | button | "/" | PLANNED |
| keybinds | keybinds..Reset.18 | Reset | button | "/" | PLANNED |
| keybinds | keybinds.key.socialInteractions | P | button | "P" | PLANNED |
| keybinds | keybinds..Reset.19 | Reset | button | "P" | PLANNED |
| keybinds | keybinds.key.attack | Left Button | button | "Left Button" | PLANNED |
| keybinds | keybinds..Reset.20 | Reset | button | "Left Button" | PLANNED |
| keybinds | keybinds.key.pickItem | Middle Button | button | "Middle Button" | PLANNED |
| keybinds | keybinds..Reset.21 | Reset | button | "Middle Button" | PLANNED |
| keybinds | keybinds.key.use | Right Button | button | "Right Button" | PLANNED |
| keybinds | keybinds..Reset.22 | Reset | button | "Right Button" | PLANNED |
| keybinds | keybinds.key.drop | Q | button | "Q" | PLANNED |
| keybinds | keybinds..Reset.23 | Reset | button | "Q" | PLANNED |
| keybinds | keybinds.key.hotbar.1 | 1 | button | "1" | PLANNED |
| keybinds | keybinds..Reset.24 | Reset | button | "1" | PLANNED |
| keybinds | keybinds.key.hotbar.2 | 2 | button | "2" | PLANNED |
| keybinds | keybinds..Reset.25 | Reset | button | "2" | PLANNED |
| keybinds | keybinds.key.hotbar.3 | 3 | button | "3" | PLANNED |
| keybinds | keybinds..Reset.26 | Reset | button | "3" | PLANNED |
| keybinds | keybinds.key.hotbar.4 | 4 | button | "4" | PLANNED |
| keybinds | keybinds..Reset.27 | Reset | button | "4" | PLANNED |
| keybinds | keybinds.key.hotbar.5 | 5 | button | "5" | PLANNED |
| keybinds | keybinds..Reset.28 | Reset | button | "5" | PLANNED |
| keybinds | keybinds.key.hotbar.6 | 6 | button | "6" | PLANNED |
| keybinds | keybinds..Reset.29 | Reset | button | "6" | PLANNED |
| keybinds | keybinds.key.hotbar.7 | 7 | button | "7" | PLANNED |
| keybinds | keybinds..Reset.30 | Reset | button | "7" | PLANNED |
| keybinds | keybinds.key.hotbar.8 | 8 | button | "8" | PLANNED |
| keybinds | keybinds..Reset.31 | Reset | button | "8" | PLANNED |
| keybinds | keybinds.key.hotbar.9 | 9 | button | "9" | PLANNED |
| keybinds | keybinds..Reset.32 | Reset | button | "9" | PLANNED |
| keybinds | keybinds.key.inventory | E | button | "E" | PLANNED |
| keybinds | keybinds..Reset.33 | Reset | button | "E" | PLANNED |
| keybinds | keybinds.key.swapOffhand | F | button | "F" | PLANNED |
| keybinds | keybinds..Reset.34 | Reset | button | "F" | PLANNED |
| keybinds | keybinds.key.loadToolbarActivator | X | button | "X" | PLANNED |
| keybinds | keybinds..Reset.35 | Reset | button | "X" | PLANNED |
| keybinds | keybinds.key.saveToolbarActivator | C | button | "C" | PLANNED |
| keybinds | keybinds..Reset.36 | Reset | button | "C" | PLANNED |
| keybinds | keybinds.key.spectatorOutlines | Not Bound | button | "Not Bound" | PLANNED |
| keybinds | keybinds..Reset.37 | Reset | button | "Not Bound" | PLANNED |
| keybinds | keybinds.key.spectatorHotbar | Middle Button | button | "Middle Button" | PLANNED |
| keybinds | keybinds..Reset.38 | Reset | button | "Middle Button" | PLANNED |
| keybinds | keybinds.key.debug.overlay | F3 | button | "F3" | PLANNED |
| keybinds | keybinds..Reset.39 | Reset | button | "F3" | PLANNED |
| keybinds | keybinds.key.debug.modifier | F3 | button | "F3" | PLANNED |
| keybinds | keybinds..Reset.40 | Reset | button | "F3" | PLANNED |
| keybinds | keybinds.key.debug.clearChat | D | button | "D" | PLANNED |
| keybinds | keybinds..Reset.41 | Reset | button | "D" | PLANNED |
| keybinds | keybinds.key.debug.copyRecreateCommand | I | button | "I" | PLANNED |
| keybinds | keybinds..Reset.42 | Reset | button | "I" | PLANNED |
| keybinds | keybinds.key.debug.copyLocation | C | button | "C" | PLANNED |
| keybinds | keybinds..Reset.43 | Reset | button | "C" | PLANNED |
| keybinds | keybinds.key.debug.spectate | N | button | "N" | PLANNED |
| keybinds | keybinds..Reset.44 | Reset | button | "N" | PLANNED |
| keybinds | keybinds.key.debug.crash | C | button | "C" | PLANNED |
| keybinds | keybinds..Reset.45 | Reset | button | "C" | PLANNED |
| keybinds | keybinds.key.debug.debugOptions | F6 | button | "F6" | PLANNED |
| keybinds | keybinds..Reset.46 | Reset | button | "F6" | PLANNED |
| keybinds | keybinds.key.debug.dumpDynamicTextures | S | button | "S" | PLANNED |
| keybinds | keybinds..Reset.47 | Reset | button | "S" | PLANNED |
| keybinds | keybinds.key.debug.dumpVersion | V | button | "V" | PLANNED |
| keybinds | keybinds..Reset.48 | Reset | button | "V" | PLANNED |
| keybinds | keybinds.key.debug.switchGameMode | F4 | button | "F4" | PLANNED |
| keybinds | keybinds..Reset.49 | Reset | button | "F4" | PLANNED |
| keybinds | keybinds.key.debug.reloadChunk | A | button | "A" | PLANNED |
| keybinds | keybinds..Reset.50 | Reset | button | "A" | PLANNED |
| keybinds | keybinds.key.debug.reloadResourcePacks | T | button | "T" | PLANNED |
| keybinds | keybinds..Reset.51 | Reset | button | "T" | PLANNED |
| keybinds | keybinds.key.debug.showAdvancedTooltips | H | button | "H" | PLANNED |
| keybinds | keybinds..Reset.52 | Reset | button | "H" | PLANNED |
| keybinds | keybinds.key.debug.showChunkBorders | G | button | "G" | PLANNED |
| keybinds | keybinds..Reset.53 | Reset | button | "G" | PLANNED |
| keybinds | keybinds.key.debug.showHitboxes | B | button | "B" | PLANNED |
| keybinds | keybinds..Reset.54 | Reset | button | "B" | PLANNED |
| keybinds | keybinds.key.debug.profiling | L | button | "L" | PLANNED |
| keybinds | keybinds..Reset.55 | Reset | button | "L" | PLANNED |
| keybinds | keybinds.key.debug.improvedTransparency | X | button | "X" | PLANNED |
| keybinds | keybinds..Reset.56 | Reset | button | "X" | PLANNED |
| keybinds | keybinds.key.debug.focusPause | P | button | "P" | PLANNED |
| keybinds | keybinds..Reset.57 | Reset | button | "P" | PLANNED |
| keybinds | keybinds.key.debug.profilingChart | 1 | button | "1" | PLANNED |
| keybinds | keybinds..Reset.58 | Reset | button | "1" | PLANNED |
| keybinds | keybinds.key.debug.fpsCharts | 2 | button | "2" | PLANNED |
| keybinds | keybinds..Reset.59 | Reset | button | "2" | PLANNED |
| keybinds | keybinds.key.debug.networkCharts | 3 | button | "3" | PLANNED |
| keybinds | keybinds..Reset.60 | Reset | button | "3" | PLANNED |
| keybinds | keybinds.key.debug.lightmapTexture | 4 | button | "4" | PLANNED |
| keybinds | keybinds..Reset.61 | Reset | button | "4" | PLANNED |
| keybinds | keybinds..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| keybinds | keybinds..Reset_Keys | Reset Keys | button | "Not applicable (command)" | INPUT_SETTINGS_IMPLEMENTED |
| keybinds | keybinds..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| linkconfirm | linkconfirm..Open_in_Browser | Open in Browser | button | "Not applicable (command)" | UI_ONLY |
| linkconfirm | linkconfirm..Copy_to_Clipboard | Copy to Clipboard | button | "Not applicable (command)" | UI_ONLY |
| linkconfirm | linkconfirm..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Turn_ON | Turn ON | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Turn_OFF | Turn OFF | button | "Not applicable (command)" | UI_ONLY |
| font | font.forceUnicodeFont | Force Unicode Font: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| font | font.japaneseGlyphVariants | Japanese Glyph Variants: ON | button | false | PREFERENCE_SAVED_EFFECT_PENDING |
| font | font..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| font | font..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..Game | Game | tab | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..World | World | tab | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..More | More | tab | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..World_Name | World Name | text | "New World" | UI_ONLY |
| create/game | create.game.draft.gameMode | Game Mode: Survival | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game.draft.difficulty | Difficulty: Normal | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game.draft.commands | Allow Cheats: OFF | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..Create_New_World | Create New World | button | "Not applicable (command)" | UI_ONLY |
| create/game | create.game..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..World_Name | World Name | text | "New World" | UI_ONLY |
| editworld | editworld..Reset_Icon | Reset Icon | button | "Not applicable (command)" | INTENTIONALLY_DISABLED |
| editworld | editworld..Open_World_Folder | Open World Folder | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Make_Backup | Make Backup | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Open_Backups_Folder | Open Backups Folder | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Optimize_World | Optimize World | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld.world-save | Save | button | "Not applicable (command)" | UI_ONLY |
| editworld | editworld..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| confirm | confirm..Delete | Delete | button | "Not applicable (command)" | UI_ONLY |
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
| rules | rules.rule.water_source_conversion | OFF | button | true | UI_ONLY |
| rules | rules..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| rules | rules.rules-done | Done | button | "Not applicable (command)" | UI_ONLY |
| rules | rules..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments.switch.Minecart_Improvements | OFF | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments.switch.Redstone_Experiments | OFF | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments.switch.Villager_Trade_Rebalance | ON | button | "Not applicable (command)" | UI_ONLY |
| experiments | experiments..Scroll | Scroll | scrollbar | 0 | UI_ONLY |
| experiments | experiments..Done | Done | button | "Not applicable (command)" | UI_ONLY |
| datapacks | datapacks..Open_Pack_Folder | Open Pack Folder | button | "Not applicable (command)" | UI_ONLY |
| datapacks | datapacks..Done | Done | button | "Not applicable (command)" | UI_ONLY |
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
| serveredit | serveredit.server.pack | Server Resource Packs: Enabled | button | "Not applicable (command)" | UI_ONLY |
| serveredit | serveredit.server-save | Done | button | "Not applicable (command)" | UI_ONLY |
| serveredit | serveredit..Cancel | Cancel | button | "Not applicable (command)" | UI_ONLY |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Move_High_Contrast_to_Available | Move High Contrast to Available | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Move_High_Contrast_up | Move High Contrast up | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.High_ContrastEnhances_the_UI_contrast_of_Minecraft.Move_High_Contrast_down | Move High Contrast down | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Move_Programmer_Art_to_Available | Move Programmer Art to Available | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Move_Programmer_Art_up | Move Programmer Art up | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
| packs | packs.Programmer_ArtThe_classic_look_of_Minecraft.Move_Programmer_Art_down | Move Programmer Art down | button | "Not applicable (command)" | PREFERENCE_SAVED_EFFECT_PENDING |
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
| confirm | confirm..Proceed | Proceed | button | "Not applicable (command)" | UI_ONLY |
| message | message..Restore_Entry | Restore Entry | button | "Not applicable (command)" | UI_ONLY |
