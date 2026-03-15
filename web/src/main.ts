import './style.css'

type Feature = {
  title: string
  body: string
}

type FaqItem = {
  question: string
  answer: string
}

const principles: Feature[] = [
  {
    title: 'Function over aesthetic',
    body: 'Impulse is built around getting out of the way: direct controls, technical visibility, and no ornamental chrome fighting the music.',
  },
  {
    title: 'Actually native',
    body: 'This is a Linux desktop app written in C++23 with ImGui, SDL3, FFmpeg, and systemd-backed MPRIS. No Chromium, React, or JavaScript.',
  },
  {
    title: 'Modern on purpose',
    body: 'Wayland support comes through SDL3, audio goes through PipeWire only, and the platform assumptions are unapologetically current.',
  },
]

const features: Feature[] = [
  {
    title: 'Gapless playback',
    body: 'Built to move cleanly between tracks, including trim and padding metadata instead of pretending every file starts and ends the same way.',
  },
  {
    title: 'ReplayGain controls',
    body: 'Track and album gain modes, configurable preamp behavior, and loudness-aware settings for people who care about consistent playback.',
  },
  {
    title: 'Playlist workflow',
    body: 'Multiple playlists, M3U8 import and export, repeat controls, URL support, and a workspace model that treats playlists like real working sets.',
  },
  {
    title: 'Session restore',
    body: 'Browser position, playlists, active tab, and other state are persisted so the app comes back where you left it.',
  },
  {
    title: 'Technical metadata',
    body: 'Codec, container, sample rate, bit depth, channel layout, file details, ReplayGain tags, and album art are surfaced instead of hidden.',
  },
  {
    title: 'Desktop integration',
    body: 'MPRIS support means media keys, desktop controls, track metadata, and open-URI flows work the way a Linux music player should.',
  },
]

const stack = [
  'C++23',
  'ImGui docking UI',
  'SDL3 for Wayland',
  'PipeWire output',
  'FFmpeg decode path',
  'systemd MPRIS',
]

const buildSteps = [
  'cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON',
  'cmake --build build-release -j"$(nproc)"',
  'ctest --test-dir build-release --output-on-failure',
]

const faqItems: FaqItem[] = [
  {
    question: 'Why does it resample everything to a fixed 48000 Hz? I thought this was supposed to be for audiophiles!',
    answer: "Because stable playback matters more than cosplay purity. Also, unless you're secretly a bat, 32 kHz is already doing plenty.",
  },
  {
    question: "It might not be Electron, but it's still another wrapper around FFmpeg.",
    answer: "Yes. FFmpeg is very good at decoding media, and rewriting FFmpeg just to feel morally pure would be an incredible waste of everybody's afternoon.",
  },
  {
    question: 'Why PipeWire only?',
    answer: 'Because modern Linux already picked a direction. Impulse follows the stack that exists now instead of dragging legacy audio paths around forever.',
  },
  {
    question: 'Why ImGui? Why not Qt, GTK, or a web stack?',
    answer: 'Because the goal is a dense, direct desktop tool, not a design system showcase. ImGui keeps the interface close to the program logic and lets the app stay lean.',
  },
  {
    question: 'Is this trying to be a pretty boutique player?',
    answer: "Not really. It's trying to be a dependable one. The aesthetic is mostly there to avoid getting in the way of playback, playlists, and technical detail.",
  },
  {
    question: 'Will it support older Linux setups and every possible backend later?',
    answer: 'Probably not by default. Impulse is intentionally opinionated: modern Linux, Wayland-era assumptions, PipeWire audio, and fewer compromises in exchange for a cleaner target.',
  },
]

const principleCards = principles
  .map(
    ({ title, body }) => `
      <article class="border border-(--line) px-5 py-5">
        <p class="text-[0.7rem] uppercase  text-(--muted)">Principle</p>
        <h3 class="mt-4 font-mono text-lg uppercase  text-(--ink)">${title}</h3>
        <p class="mt-3 max-w-lg text-sm leading-6 text-(--soft)">${body}</p>
      </article>
    `,
  )
  .join('')

const featureCards = features
  .map(
    ({ title, body }) => `
      <article class="flex min-h-52 flex-col justify-between border border-(--line) px-5 py-5">
        <div>
          <p class="text-[0.68rem] uppercase  text-(--muted)">Feature</p>
          <h3 class="mt-3 font-mono text-base uppercase  text-(--ink)">${title}</h3>
          <p class="mt-3 text-sm leading-6 text-(--soft)">${body}</p>
        </div>
      </article>
    `,
  )
  .join('')

const stackItems = stack
  .map(
    (item) => `
      <li class="border-t border-(--line) py-3 font-mono text-sm uppercase  text-(--ink)">${item}</li>
    `,
  )
  .join('')

const buildCommands = buildSteps
  .map((step) => `<span class="block">${step}</span>`)
  .join('')

const faqMarkup = faqItems
  .map(
    ({ question, answer }) => `
      <article class="border-t border-(--line) py-5 first:border-t-0 first:pt-0 last:pb-0">
        <p class="text-[0.68rem] uppercase text-(--muted)">Q</p>
        <h3 class="mt-2 max-w-3xl font-mono text-base text-(--ink)">${question}</h3>
        <p class="mt-4 max-w-3xl text-sm leading-6 text-(--soft)">${answer}</p>
      </article>
    `,
  )
  .join('')

document.querySelector<HTMLDivElement>('#app')!.innerHTML = `
  <div class="relative isolate">
    <header class="border-b border-(--line)">
      <div class="mx-auto flex max-w-7xl flex-col gap-4 px-6 py-5 md:flex-row md:items-center md:justify-between md:px-10">
        <div>
          <a href="#top" class="font-mono text-sm uppercase  text-(--ink)">Impulse</a>
          <p class="mt-1 text-xs uppercase  text-(--muted)">Linux-native music player</p>
        </div>
        <nav class="flex flex-wrap gap-x-6 gap-y-2 text-xs uppercase  text-(--muted)">
          <a href="#why" class="transition-colors hover:text-(--ink)">Why</a>
          <a href="#features" class="transition-colors hover:text-(--ink)">Features</a>
          <a href="#stack" class="transition-colors hover:text-(--ink)">Stack</a>
          <a href="#faq" class="transition-colors hover:text-(--ink)">FAQ</a>
          <a href="#build" class="transition-colors hover:text-(--ink)">Build</a>
          <a href="https://github.com/kylxbn/impulse" target="_blank" rel="noreferrer" class="transition-colors hover:text-(--ink)">GitHub</a>
        </nav>
      </div>
    </header>

    <main id="top">
      <section class="mx-auto grid max-w-7xl gap-10 px-6 py-12 md:px-10 lg:grid-cols-[minmax(0,1.05fr)_minmax(0,0.95fr)] lg:py-20">
        <div class="flex flex-col justify-between">
          <div>
            <h1 class="mt-6 max-w-4xl font-mono text-5xl uppercase leading-[0.92]  text-(--ink) sm:text-6xl lg:text-7xl">
              A technical music player with no patience for Electron.
            </h1>
            <p class="mt-6 max-w-2xl text-base leading-7 text-(--soft) sm:text-lg">
              Impulse is a desktop player for people who want stable playback, clear controls, real metadata, and a native Linux app that treats modern desktop plumbing as the baseline.
            </p>
          </div>

          <div class="mt-10 flex flex-col gap-4 sm:flex-row">
            <a href="#features" class="border border-(--ink) px-5 py-3 text-center font-mono text-xs uppercase  text-(--ink) transition-colors hover:bg-(--ink) hover:text-(--paper)">
              Explore features
            </a>
            <a href="#build" class="border border-(--line-strong) px-5 py-3 text-center font-mono text-xs uppercase  text-(--ink) transition-colors hover:border-(--ink)">
              Build from source
            </a>
            <a href="https://github.com/kylxbn/impulse" target="_blank" rel="noreferrer" class="border border-(--line-strong) px-5 py-3 text-center font-mono text-xs uppercase  text-(--ink) transition-colors hover:border-(--ink)">
              GitHub
            </a>
          </div>

          <dl class="mt-10 grid gap-px border border-(--line) bg-(--line) text-sm sm:grid-cols-3">
            <div class="bg-(--panel) px-4 py-4">
              <dt class="text-[0.68rem] uppercase  text-(--muted)">Rendering</dt>
              <dd class="mt-2 font-mono uppercase  text-(--ink)">ImGui + SDL3</dd>
            </div>
            <div class="bg-(--panel) px-4 py-4">
              <dt class="text-[0.68rem] uppercase  text-(--muted)">Audio path</dt>
              <dd class="mt-2 font-mono uppercase  text-(--ink)">FFmpeg + PipeWire</dd>
            </div>
            <div class="bg-(--panel) px-4 py-4">
              <dt class="text-[0.68rem] uppercase  text-(--muted)">Desktop fit</dt>
              <dd class="mt-2 font-mono uppercase  text-(--ink)">Wayland + MPRIS</dd>
            </div>
          </dl>
        </div>

        <div class="grid gap-4">
          <figure class="border border-(--line-strong) bg-(--panel-strong)">
            <div class="flex items-center justify-between border-b border-(--line) px-4 py-3 text-[0.68rem] uppercase  text-(--muted)">
              <span>Current interface</span>
              <span>Main window</span>
            </div>
            <img
              src="screenshot.png"
              alt="Screenshot of the Impulse desktop application"
              class="block w-full border-t-0"
            />
          </figure>

          <div class="border border-(--line) bg-(--panel)">
            <div class="px-4 py-5">
              <p class="text-[0.68rem] uppercase  text-(--muted)">Core idea</p>
              <p class="mt-3 text-sm leading-6 text-(--soft)">
                Designed around function first: fast playlist work, readable technical context, and the kind of platform choices you make when Linux is the target, not an afterthought.
              </p>
            </div>
          </div>
        </div>
      </section>

      <section id="why" class="border-y border-(--line) bg-(--panel)">
        <div class="mx-auto max-w-7xl px-6 py-12 md:px-10 lg:py-16">
          <div class="max-w-3xl">
            <p class="text-[0.7rem] uppercase  text-(--signal)">Why Impulse exists</p>
            <h2 class="mt-4 font-mono text-3xl uppercase  text-(--ink) sm:text-4xl">
              Built for people who want the player to work harder than the interface.
            </h2>
            <p class="mt-5 text-base leading-7 text-(--soft)">
              Impulse is for listeners who care about playback behavior, metadata, playlists, and desktop integration more than animation polish. The visual language stays restrained so the technical choices can be the headline.
            </p>
          </div>

          <div class="mt-10 grid gap-4 lg:grid-cols-3">
            ${principleCards}
          </div>
        </div>
      </section>

      <section id="features" class="mx-auto max-w-7xl px-6 py-12 md:px-10 lg:py-18">
        <div class="grid gap-8 lg:grid-cols-[minmax(0,0.55fr)_minmax(0,1fr)]">
          <div>
            <p class="text-[0.7rem] uppercase  text-(--signal)">What it offers</p>
            <h2 class="mt-4 font-mono text-3xl uppercase  text-(--ink) sm:text-4xl">
              Native playback features, not a dressed-up shell.
            </h2>
            <p class="mt-5 max-w-xl text-base leading-7 text-(--soft)">
              The codebase already exposes the details a technical listener expects: gapless transitions, ReplayGain controls, playlist management, session recovery, file browsing, and real MPRIS integration.
            </p>
          </div>

          <div class="grid gap-4 sm:grid-cols-2">
            ${featureCards}
          </div>
        </div>
      </section>

      <section id="stack" class="border-y border-(--line) bg-(--panel)">
        <div class="mx-auto grid max-w-7xl gap-10 px-6 py-12 md:px-10 lg:grid-cols-[minmax(0,0.8fr)_minmax(0,1.2fr)] lg:py-16">
          <div>
            <p class="text-[0.7rem] uppercase  text-(--signal)">What sets it apart</p>
            <h2 class="mt-4 font-mono text-3xl uppercase  text-(--ink) sm:text-4xl">
              Modern Linux assumptions all the way down.
            </h2>
            <p class="mt-5 max-w-xl text-base leading-7 text-(--soft)">
              Impulse is not chasing universal compatibility. It chooses current Linux foundations and keeps the stack compact, explicit, and fast instead of carrying the weight of a browser runtime.
            </p>
          </div>

          <div class="grid gap-8 lg:grid-cols-[0.9fr_1.1fr]">
            <div class="border border-(--line) px-5 py-5">
              <p class="text-[0.68rem] uppercase  text-(--muted)">Stack</p>
              <ul class="mt-4">
                ${stackItems}
              </ul>
            </div>

            <div class="border border-(--line) px-5 py-5">
              <p class="text-[0.68rem] uppercase  text-(--muted)">Positioning</p>
              <div class="mt-4 space-y-4 text-sm leading-6 text-(--soft)">
                <p>
                  Electron gives you reach; Impulse chooses responsiveness, direct platform access, and a lighter footprint instead.
                </p>
                <p>
                  PipeWire-only audio support keeps the output path aligned with the modern Linux desktop rather than stretching to legacy backends.
                </p>
                <p>
                  SDL3 gives the project a clean route into Wayland-era desktops while ImGui keeps the UI direct, dense, and practical.
                </p>
              </div>
            </div>
          </div>
        </div>
      </section>

      <section id="faq" class="mx-auto max-w-7xl px-6 py-12 md:px-10 lg:py-18">
        <div class="grid gap-8 lg:grid-cols-[minmax(0,0.7fr)_minmax(0,1fr)]">
          <div>
            <p class="text-[0.7rem] uppercase  text-(--signal)">FAQ</p>
            <h2 class="mt-4 font-mono text-3xl uppercase  text-(--ink) sm:text-4xl">
              A few things people might ask.
            </h2>
            <p class="mt-5 max-w-xl text-base leading-7 text-(--soft)">
              Some of these are sincere questions. Some of them are very online questions. Both deserve answers.
            </p>
          </div>

          <div class="border border-(--line) bg-(--panel) px-5 py-5">
            ${faqMarkup}
          </div>
        </div>
      </section>

      <section id="build" class="border-y border-(--line) bg-(--panel)">
        <div class="mx-auto max-w-7xl px-6 py-12 md:px-10 lg:py-18">
          <div class="grid gap-8 lg:grid-cols-[minmax(0,0.7fr)_minmax(0,1fr)]">
            <div>
              <p class="text-[0.7rem] uppercase  text-(--signal)">Build</p>
              <h2 class="mt-4 font-mono text-3xl uppercase  text-(--ink) sm:text-4xl">
                Source-first, straightforward, and honest about its dependencies.
              </h2>
              <p class="mt-5 max-w-xl text-base leading-7 text-(--soft)">
                Impulse depends on FFmpeg, PipeWire, SDL3, and systemd-libs at runtime, then builds cleanly with CMake. The project is GPL-3.0-only and aimed squarely at Linux users.
              </p>
            </div>

            <div class="min-w-0 border border-(--line-strong) bg-(--panel-strong)">
              <div class="flex items-center justify-between border-b border-(--line) px-4 py-3 text-[0.68rem] uppercase  text-(--muted)">
                <span>Release build</span>
                <span>From README</span>
              </div>
              <pre class="w-full overflow-x-auto px-4 py-5 font-mono text-sm leading-7 text-(--ink)"><code>${buildCommands}</code></pre>
            </div>
          </div>
        </div>
      </section>
    </main>

    <footer class="border-t border-(--line)">
      <div class="mx-auto flex max-w-7xl flex-col gap-3 px-6 py-6 text-sm text-(--muted) md:flex-row md:items-center md:justify-between md:px-10">
        <p>Impulse is a Linux-native desktop music player focused on technical playback and practical control.</p>
        <a href="https://github.com/kylxbn/impulse" target="_blank" rel="noreferrer" class="font-mono uppercase  text-(--ink)">
          github.com/kylxbn/impulse
        </a>
      </div>
    </footer>
  </div>
`
