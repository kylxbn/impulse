import './style.css'

import imgScreenshot from './img/screenshot.webp'
import imgWideFormatSupport from './img/wide_format_support.webp'
import imgTechnicalInfoInspector from './img/technical_info_inspector.webp'
import imgPeakBitrateBuffer from './img/peak_bitrate_buffer.webp'
import imgMultiplePlaylistTabs from './img/multiple_playlist_tabs.webp'
import imgFileBrowser from './img/file_browser.webp'
import imgSettings from './img/settings.webp'
import imgMpris from './img/mpris.webp'

// Set image sources (Vite hashes these for cache busting)
document.getElementById('img-screenshot').src = imgScreenshot
document.getElementById('img-wide-format').src = imgWideFormatSupport
document.getElementById('img-inspector').src = imgTechnicalInfoInspector
document.getElementById('img-peak').src = imgPeakBitrateBuffer
document.getElementById('img-playlists').src = imgMultiplePlaylistTabs
document.getElementById('img-browser').src = imgFileBrowser
document.getElementById('img-settings').src = imgSettings
document.getElementById('img-mpris').src = imgMpris

const content = {
  en: {
    nav_github: 'GitHub',

    hero_tagline: 'A music player for technical people.',
    hero_specs: '48 kHz · float32 ring buffer · PipeWire direct output',
    hero_cta: 'View on GitHub',
    hero_alt_screenshot: 'Impulse music player main window',

    feat_formats_label: 'FORMAT SUPPORT',
    feat_formats_title: 'Wide Format Support',
    feat_formats_desc:
      'Decoded via FFmpeg (MP3, FLAC, OPUS, WMA, APE, DSD, DTS, TrueHD, Dolby AC-4, and more), OpenMPT with no interpolation and no surround effects (XM, MOD, IT, S3M, MED, OKT, UMX, MTM, and more), libvgm (VGM/VGZ), and sc68 (Atari ST). If the backend can read it, Impulse will play it.',
    feat_formats_alt: 'File format support list',

    feat_inspector_label: 'TECHNICAL INSPECTION',
    feat_inspector_title: 'Per-track Technical Info',
    feat_inspector_desc:
      'Codec, sample rate, bitrate, channel layout, ReplayGain track/album gain and peak, and Peak Loudness Range (PLR) are shown per track in the inspector window. Metadata is read via FFmpeg.',
    feat_inspector_alt: 'Technical information inspector window',

    feat_gapless_label: 'GAPLESS PLAYBACK',
    feat_gapless_title: 'True Gapless',
    feat_gapless_desc:
      'Tracks are pre-decoded into a shared float32 ring buffer (2M samples — approximately 21.8 seconds at 48 kHz stereo) before the current track ends. Gapless is best-effort and depends on FFmpeg decode accuracy.',

    feat_peak_label: 'REAL-TIME BUFFER VISUALIZATION',
    feat_peak_title: 'Ring Buffer & Bitrate',
    feat_peak_desc:
      'The inspector displays the current fill state of the ring buffer alongside the instantaneous and peak bitrate. A persistent clipping indicator is shown when the audio signal exceeds 0 dBFS.',
    feat_peak_alt: 'Ring buffer and peak bitrate visualization strip',

    feat_playlists_label: 'PLAYLIST MANAGEMENT',
    feat_playlists_title: 'Multiple Playlist Tabs',
    feat_playlists_desc:
      'Multiple named playlists in dockable tabs. Tracks can be sorted by order, track number, album, title, artist, duration, codec, bitrate, ReplayGain, PLR, filename, or path. Saved and loaded as M3U8.',
    feat_playlists_alt: 'Multiple playlist tabs view',

    feat_browser_label: 'FILE BROWSER',
    feat_browser_title: 'Filesystem Browser',
    feat_browser_desc:
      'Browse the local filesystem from a dockable panel. No library database, no scanning step. Directories and files are listed and added directly to the active playlist.',
    feat_browser_alt: 'File browser panel',

    feat_settings_label: 'SETTINGS',
    feat_settings_title: 'Settings',
    feat_settings_desc:
      'Configure ReplayGain mode (none, track, album) and reference LUFS, repeat behavior, and the PipeWire audio output. Settings and playlist state are persisted across sessions.',
    feat_settings_alt: 'Settings window',

    mpris_label: 'DESKTOP INTEGRATION',
    mpris_title: 'MPRIS Support',
    mpris_desc:
      'Full MPRIS2 implementation via D-Bus for integration with KDE, GNOME, and other desktop environments that support the Media Player Remote Interfacing Specification.',
    mpris_alt: 'MPRIS desktop integration in action',

    page_title: 'Impulse — A music player for technical people',
    footer_line: 'Linux · PipeWire · C++23 · GPL-3.0-only · v1.4.1',
    footer_github: 'View on GitHub',
  },
  ja: {
    nav_github: 'GitHub',

    hero_tagline: '技術者のためのミュージックプレーヤー。',
    hero_specs: '48 kHz・float32リングバッファ・PipeWireダイレクト出力',
    hero_cta: 'GitHubで見る',
    hero_alt_screenshot: 'Impulseミュージックプレーヤーのメインウィンドウ',

    feat_formats_label: 'フォーマット対応',
    feat_formats_title: '幅広いフォーマット対応',
    feat_formats_desc:
      'FFmpeg経由（MP3、FLAC、OPUS、WMA、APE、DSD、DTS、TrueHD、Dolby AC-4など）、OpenMPT経由（XM、MOD、IT、S3M、MED、OKT、UMX、MTMなど）、libvgm（VGM/VGZ）、sc68（Atari ST）でデコード。バックエンドが対応している形式はImpulseで再生できる。',
    feat_formats_alt: 'ファイルフォーマット対応一覧',

    feat_inspector_label: '技術情報インスペクター',
    feat_inspector_title: 'トラックごとの技術情報',
    feat_inspector_desc:
      'インスペクターウィンドウにはトラックごとのコーデック、サンプルレート、ビットレート、チャンネルレイアウト、ReplayGainのトラック/アルバムゲインとピーク、ピークラウドネスレンジ（PLR）が表示される。メタデータはFFmpegで読み取られる。',
    feat_inspector_alt: '技術情報インスペクターウィンドウ',

    feat_gapless_label: 'ギャップレス再生',
    feat_gapless_title: '真のギャップレス再生',
    feat_gapless_desc:
      '次のトラックは現在のトラックが終わる前に共有float32リングバッファ（2Mサンプル、48kHzステレオで約21.8秒分）へ事前デコードされる。ギャップレス再生はFFmpegのデコード精度に依存したベストエフォートで行われる。',

    feat_peak_label: 'リアルタイムバッファ可視化',
    feat_peak_title: 'リングバッファとビットレート',
    feat_peak_desc:
      'インスペクターにはリングバッファの現在の充填状態と瞬時・ピークビットレートが表示される。音声信号が0 dBFSを超えた場合はクリッピングインジケーターが継続表示される。',
    feat_peak_alt: 'リングバッファとピークビットレートの可視化ストリップ',

    feat_playlists_label: 'プレイリスト管理',
    feat_playlists_title: '複数のプレイリストタブ',
    feat_playlists_desc:
      'ドッキング可能なタブで複数の名前付きプレイリストを管理。順序・トラック番号・アルバム・タイトル・アーティスト・時間・コーデック・ビットレート・ReplayGain・PLR・ファイル名・パスでソート可能。M3U8形式で保存・読み込み。',
    feat_playlists_alt: '複数プレイリストタブの表示',

    feat_browser_label: 'ファイルブラウザー',
    feat_browser_title: 'ファイルシステムブラウザー',
    feat_browser_desc:
      'ドッキング可能なパネルからローカルファイルシステムを操作できる。ライブラリデータベースもスキャン処理も不要。ディレクトリとファイルを一覧表示し、アクティブなプレイリストへ直接追加できる。',
    feat_browser_alt: 'ファイルブラウザーパネル',

    feat_settings_label: '設定',
    feat_settings_title: '設定',
    feat_settings_desc:
      'ReplayGainモード（なし、トラック、アルバム）と参照LUFS、リピート動作、PipeWire音声出力を設定できる。設定とプレイリストの状態はセッション間で保持される。',
    feat_settings_alt: '設定ウィンドウ',

    mpris_label: 'デスクトップ統合',
    mpris_title: 'MPRISサポート',
    mpris_desc:
      'D-Bus経由のフルMPRIS2実装により、KDE、GNOME、その他のMedia Player Remote Interfacing Specification対応デスクトップ環境と統合できる。',
    mpris_alt: 'MPRISデスクトップ統合の動作例',

    page_title: 'Impulse — 技術者のためのミュージックプレーヤー',
    footer_line: 'Linux・PipeWire・C++23・GPL-3.0-only・v1.4.1',
    footer_github: 'GitHubで見る',
  },
}

function applyLang(lang) {
  const t = content[lang]

  document.documentElement.lang = lang
  document.title = t.page_title

  document.querySelectorAll('[data-i18n]').forEach((el) => {
    const key = el.dataset.i18n
    if (t[key] !== undefined) el.textContent = t[key]
  })

  document.querySelectorAll('[data-i18n-alt]').forEach((el) => {
    const key = el.dataset.i18nAlt
    if (t[key] !== undefined) el.alt = t[key]
  })

  document.querySelectorAll('[data-lang-btn]').forEach((btn) => {
    const active = btn.dataset.langBtn === lang
    btn.classList.toggle('bg-black', active)
    btn.classList.toggle('text-white', active)
    btn.classList.toggle('bg-white', !active)
    btn.classList.toggle('text-black', !active)
  })
}

document.querySelectorAll('[data-lang-btn]').forEach((btn) => {
  btn.addEventListener('click', () => applyLang(btn.dataset.langBtn))
})

applyLang('en')
