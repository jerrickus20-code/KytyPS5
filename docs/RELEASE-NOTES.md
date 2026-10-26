## This fork: cheats, in-game switches and late DLSS

Built on v20261026 (its notes follow). See README.md, "Late DLSS and the cheat keys".

- **Late DLSS** (`run-latedlss-1440-cheats.cmd`): the 3D at 1280x720 without the game's TAA, its post-processing at
  720, then one DLSS pass anti-aliases and upscales the finished picture to 2560x1440 before the HUD goes over it.
  Needs DLSS frame generation (NVIDIA's Streamline runtime). Shift+F6 switches it off to compare.
- **Late DLSS at a 1440p output**: the game's output is 2560x1440 with it (its HD mode at that size, the HUD in its 4K
  layout drawn at 1440), not 4K: DLSS's picture reaches the screen without being stretched to 4K and scaled down again,
  and the 4K composite and HUD are not drawn. `set KYTY_LATE_DLSS_OUTPUT=4k` in the console before the script keeps
  the 4K output to compare.
- **In-game switches**, remembered in `_dlss\toggles.txt`: F4 the 30 fps cap, F12 the FPS panel, F6 DLAA, Shift+F6
  late DLSS. A short note on screen says the new state.
- **Cheats**: F1-F3, F5, F11 cheat switches (`KYTY_CHEAT_KEYS`) and F10's give items panel (`KYTY_CHEAT_GIVE_SLOT`).
  Put your cheat files in a `cheats` folder next to the launcher; it runs without them.

## New in this build

Your precompiled shaders stay valid: the shaders are the same as in v20261023. The precompile only adds the pipelines of
the new shader hints (#212).

- **Crash on version 1.005** (#185, #204): on some PCs Demon's Souls 1.005 (European and US editions) crashed within
  minutes of play, or already at start-up. The game's resource loader could take a load request that another thread
  was withdrawing at that moment. The emulator now makes the loader wait until the withdrawal is finished; the log line
  `Demon's Souls loader guard: ... waited out` shows each time it did.
- **Blue screens on 16 GB PCs** (#91): since v20261024, the pages the game reads back from the graphics card were
  protected in a way that could crash Windows (MEMORY_MANAGEMENT) once the PC ran low on RAM. Whenever less than 2 GB
  of RAM is free they now get guard pages instead, which held up under heavy memory pressure in our tests.
- **Shader preparation kept on disk** (#200): the background shader preparation and the start-up warmup now keep their
  translations in `_PipelineCache\static\<title>_<version>.prefetch` (1-2 GB). The first start after this update
  translates as before; later starts read them in about a second. On our desktop the start-up warmup went from 22 s to
  1 s, and PCs with few cores no longer translate for minutes beside the game. `run.cmd -Set KYTY_PREFETCH_CACHE=0`
  turns it off. After a patch by @Psychowarden.
- **Lit ground and water flickering white** (#196): buffer copies made on a background thread could take data the game
  had already replaced for its next frame. The copies now finish before the game can see that the graphics card is
  done with them. Tracked down with @Psychowarden's tests.
- **Frame generation at camera cuts** (#174): when a dialogue or a cutscene shot ended, the frame generated in between
  blended the two camera views, both half-transparent. With an upscaler chosen (DLSS or FSR), frame generation now
  starts over at the game's camera cuts.
- **The 1440p output in the 4K layout** (#187, #202): at the 1440p output the interface is laid out as at 4K, at the
  output's size, instead of the 1080p layout scaled up: sharp text, and the character in the character creator and the
  equipment menu in place. `run.cmd -Set KYTY_OUTPUT_LAYOUT=hd` keeps the old way.
- **Wide screens at 1440p** (#199, #198): a 21:9 or wider aspect with the 1440p output now makes the picture at that
  height (3416x1440 at 21:9) instead of 16:9 (77 -> 90 fps on an RTX 5080 at 43:18), and movies are no longer squeezed
  with a wide output. After @Psychowarden's patches.
- **Audio output** (#207): the launcher's Game page has "Audio output": TV, headphones, AV amplifier or soundbar. The
  game is told your choice and mixes its sound for it, as on a PS5 (`run.cmd -Set KYTY_AUDIO_OUTPUT=headphones`). Found
  by @Psychowarden.
- **Quitting with a controller** (#203): hold the PS button (an Xbox pad's Guide button) for two seconds to quit, as
  Close Game does on a PS5. Where Steam or the Game Bar takes that button, `run.cmd -Set KYTY_PAD_QUIT=both` also quits
  on Create and Options (an Xbox pad's View and Menu) held together. An Xbox pad's View button now presses the touchpad
  (#197).
- **More shader hints** (#212): a playthrough to both endings added 237 shaders and 268 pipelines the release's hints
  lacked: fewer first-time stutters late in the game. Thanks to @Vorshim92.
- **Precompile** (#213): a shard that stops working no longer holds up the whole precompile. After 20 minutes without
  work it is started again, twice at most, and then left out; the game compiles its pipelines when it first needs them.
- **Less memory on 16 GB PCs**: memory the game's engine has freed goes back to Windows four times faster: 0.8 GB less
  in a long session.
- **New images start cleared** (PR #193, @intellq): as on the PS5, a new image is zero before its first use. With FSR
  at 1080p and the 43:18 squeeze, its first frame read undefined contents and kept a green or red picture for the whole
  session, mostly on Linux with AMD cards.
- **Linux**: AMD graphics cards (RADV) now get their transfer queue, so readbacks of finished GPU work no longer wait
  behind the graphics queue: +19% to +27% while walking on an RX 9060 XT and RX 9070 XT. Found by @the2ndday, tested by
  @dreamerk24m and @yazuc. `-Set KYTY_RADV_TRANSFER_QUEUE=0` turns it off; owners of older AMD cards, please report any
  GPU hang. Also from @the2ndday: an experimental `KYTY_UFFD_WP=1` (the GPU trackers' write watch by userfaultfd,
  Linux 6.4+; PR #206), and the bench helpers on Linux (PR #211).
- **Thanks** to @Psychowarden, @the2ndday, @Vorshim92 and @intellq for their patches and pull requests, and to
  @danielmazurek79, @Theoryedz, @ededaelita1-afk, @ft1704, @dreamerk24m, @Fantomas3228, @yazuc, @marcelofis,
  @SanyaSan and @kvothe3323 for their reports and tests.

## New in v20261025b

A fix release for v20261025. Your precompiled shaders stay valid: the shaders are the same as in v20261023.

- **Stutter on PCs with 16-20 GB of RAM** (#5): v20261025 gives the memory the game's engine has freed back to
  Windows, and holds the engine's own memory lock while it does. On a PC short of memory, Windows was slow at each
  step and the game waited for that lock most of the time: 10-15 fps for many seconds, and short freezes. Now the
  give-back holds the lock at most 2.5% of the time, pauses for 30 seconds to 2 minutes while Windows is slow, and
  only takes back memory that has stayed free for 30 seconds. If you added `-Set KYTY_ENGINE_FREE=0` because of this,
  you can remove it.
- **About 0.5 GB less RAM on PCs with 20 GB or less**: the background shader preparation kept a ready-made part (the
  code that reads a shader's descriptors) for every one of the game's ~22,000 shaders, of which a session uses a few
  thousand. It now makes it when the game first uses a shader (~0.4 ms each).
- **AMD: fullscreen as fast as a window** (#166): on AMD graphics cards the emulator now asks the driver not to use
  exclusive fullscreen, which ran ~10% slower than a window on an RX 9060 XT. `run.cmd -Set KYTY_FULLSCREEN_EXCLUSIVE=1`
  allows it again.
- **Thanks** to @sklektr and @berkocevik-del for their reports and tests.

## 本版本的变化

已有的预编译着色器继续有效：着色器与 v20261023 相同。预编译只会补上新增着色器提示的管线（#212）。

- **1.005 版本的崩溃**（#185、#204）：在部分电脑上，《恶魔之魂》1.005（欧版和美版）游玩几分钟后、甚至启动时就会崩溃。
  游戏的资源加载器可能取到另一个线程正在撤回的加载请求。模拟器现在让加载器等撤回完成再继续；每次起作用时，日志里会出现
  `Demon's Souls loader guard: ... waited out`。
- **16 GB 电脑蓝屏**（#91）：从 v20261024 起，游戏从显卡读回的内存页所用的保护方式，在内存不足时可能让 Windows 蓝屏
  （MEMORY_MANAGEMENT）。现在只要可用内存少于 2 GB，这些页就改用保护页（guard page），它在我们的高内存压力测试中没有出现问题。
- **着色器准备保存到磁盘**（#200）：后台着色器准备和启动预热现在把翻译结果保存在 `_PipelineCache\static\<游戏>_<版本>.prefetch`
  （1-2 GB）。更新后第一次启动照常翻译；之后的启动约一秒就能读完。我们台式机上的启动预热从 22 秒变为 1 秒，核心少的
  电脑也不用在游戏旁边翻译好几分钟了。`run.cmd -Set KYTY_PREFETCH_CACHE=0` 可以关闭。基于 @Psychowarden 的补丁。
- **受光照的地面和水面闪白**（#196）：后台线程复制缓冲区时，可能取到游戏已经为下一帧改写的数据。现在复制会在游戏得知显卡
  用完这些数据之前完成。靠 @Psychowarden 的测试找到原因。
- **镜头切换时的插帧**（#174）：对话或过场镜头结束时，中间插出的那一帧会把两个镜头的画面混在一起（都半透明）。选择了超分
  （DLSS 或 FSR）时，插帧现在会在游戏的镜头切换处重新开始。
- **1440p 输出使用 4K 界面布局**（#187、#202）：1440p 输出时，界面按 4K 布局、以输出尺寸排版，而不是把 1080p 布局放大：
  文字清晰，角色创建和装备菜单里的角色也在正确位置。`run.cmd -Set KYTY_OUTPUT_LAYOUT=hd` 保留旧方式。
- **1440p 下的宽屏**（#199、#198）：21:9 或更宽的比例配合 1440p 输出时，画面现在按这个高度生成（21:9 时为 3416x1440），
  而不是 16:9（RTX 5080 在 43:18 下从 77 fps 提升到 90 fps）；宽屏输出时过场影片也不再被压扁。基于 @Psychowarden 的补丁。
- **音频输出**（#207）：启动器「游戏」页新增「音频输出」：电视、耳机、AV 功放或条形音箱。游戏会得知你的选择并据此混音，与
  PS5 相同（`run.cmd -Set KYTY_AUDIO_OUTPUT=headphones`）。由 @Psychowarden 发现。
- **用手柄退出游戏**（#203）：按住 PS 键（Xbox 手柄的 Guide 键）两秒即可退出，与 PS5 的「关闭游戏」相同。如果 Steam 或
  Game Bar 占用了这个键，`run.cmd -Set KYTY_PAD_QUIT=both` 让同时按住 Create 和 Options（Xbox 手柄的 View 和 Menu）也能
  退出。Xbox 手柄的 View 键现在等同于按下触摸板（#197）。
- **更多着色器提示**（#212）：一份打到两个结局的完整游玩记录补上了发布包提示中没有的 237 个着色器和 268 条管线：游戏后期
  第一次出现的卡顿更少。感谢 @Vorshim92。
- **预编译**（#213）：某个分片停止工作时不再拖住整个预编译。它 20 分钟没有进展就会重新启动（最多两次），之后跳过；游戏在
  第一次需要时再编译这些管线。
- **16 GB 电脑内存占用更少**：游戏引擎释放的内存归还给 Windows 的速度快了四倍：长时间游玩少占 0.8 GB。
- **新建图像先清零**（PR #193，@intellq）：与 PS5 一样，新图像在第一次使用前为零。以前在 1080p 的 FSR 加 43:18 压缩下，
  第一帧会读到未定义的内容，整局都保持绿色或红色的画面，主要出现在 Linux 的 AMD 显卡上。
- **Linux**：AMD 显卡（RADV）现在会启用传输队列，读回显卡已完成的数据时不再排在整个图形队列后面：RX 9060 XT 和
  RX 9070 XT 行走时帧率提升 19% 到 27%。由 @the2ndday 发现，@dreamerk24m 和 @yazuc 测试。`-Set KYTY_RADV_TRANSFER_QUEUE=0`
  可以关闭；较旧 AMD 显卡的用户如果遇到显卡挂起请反馈。同样来自 @the2ndday：实验性的 `KYTY_UFFD_WP=1`（用 userfaultfd 监视
  GPU 跟踪的写入，需要 Linux 6.4+；PR #206），以及 Linux 上的测试辅助命令（PR #211）。
- **感谢** @Psychowarden、@the2ndday、@Vorshim92 和 @intellq 的补丁与 PR，以及 @danielmazurek79、@Theoryedz、
  @ededaelita1-afk、@ft1704、@dreamerk24m、@Fantomas3228、@yazuc、@marcelofis、@SanyaSan 和 @kvothe3323 的反馈与测试。

## v20261025b 的变化

v20261025 的修正版。已有的预编译着色器继续有效：着色器与 v20261023 相同。

- **16-20 GB 内存电脑上的卡顿**（#5）：v20261025 会把游戏引擎释放的内存还给 Windows，并在此期间持有引擎自己的内存锁。在
  内存紧张的电脑上，Windows 处理每一步都很慢，游戏大部分时间都在等这把锁：连续很多秒只有 10-15 fps，还有短暂的卡死。现在
  归还内存最多占用这把锁 2.5% 的时间，Windows 忙时暂停 30 秒到 2 分钟，并且只回收空闲了 30 秒的内存。如果你因为这个问题
  加了 `-Set KYTY_ENGINE_FREE=0`，现在可以去掉。
- **20 GB 及以下内存的电脑少用约 0.5 GB**：后台着色器准备为游戏的约 22000 个着色器都预先生成了一部分（读取着色器描述符的
  代码），而一次游戏只会用到其中几千个。现在改为游戏第一次使用某个着色器时再生成（每个约 0.4 毫秒）。
- **AMD：全屏和窗口一样快**（#166）：在 AMD 显卡上，模拟器现在请求驱动不使用独占全屏；在 RX 9060 XT 上独占全屏比窗口慢约
  10%。`run.cmd -Set KYTY_FULLSCREEN_EXCLUSIVE=1` 恢复允许独占全屏。
- **感谢** @sklektr 和 @berkocevik-del 的报告和测试。
