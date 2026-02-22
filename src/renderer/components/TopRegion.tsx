import { memo, type JSX } from "react";
import { formatDuration } from "../../shared/format";
import type { PlaybackSnapshot, RepeatMode } from "../../shared/types";
import noCoverImage from "../assets/no-cover.png";
import styles from "./TopRegion.module.css";

interface TopRegionProps {
  coverArtDataUrl: string | null;
  nowPlaying: {
    artistAndAlbum: string;
    title: string;
  };
  playback: PlaybackSnapshot;
  trackReadout: string;
  onSetRepeatMode(mode: RepeatMode): void;
  onSetShuffle(enabled: boolean): void;
  onPrevious(): void;
  onStop(): void;
  onPlayPause(): void;
  onNext(): void;
  onSeekAbsolute(seconds: number): void;
  onSetVolume(percent: number): void;
  onOpenAlbumArt(): void;
}

function TopRegionComponent({
  coverArtDataUrl,
  nowPlaying,
  playback,
  trackReadout,
  onSetRepeatMode,
  onSetShuffle,
  onPrevious,
  onStop,
  onPlayPause,
  onNext,
  onSeekAbsolute,
  onSetVolume,
  onOpenAlbumArt
}: TopRegionProps): JSX.Element {
  return (
    <header className={styles.topRegion}>
      <div className={styles.topMetaRow}>
        <div className={styles.topMetaLeft}>
          <div className={styles.albumArtContainer}>
            {coverArtDataUrl ? (
              <button
                type="button"
                className={styles.albumArtButton}
                onClick={onOpenAlbumArt}
                aria-label="Open album art"
                title="Open album art"
              >
                <img
                  className={styles.albumArtImage}
                  src={coverArtDataUrl}
                  alt={`${nowPlaying.title} album art`}
                />
              </button>
            ) : (
              <img
                className={styles.albumArtImage}
                src={noCoverImage}
                alt="No cover artwork"
                draggable={false}
              />
            )}
          </div>

          <div className={styles.nowPlaying} title={`${nowPlaying.artistAndAlbum}\n${nowPlaying.title}`}>
            <span className={`${styles.nowPlayingLine} ${styles.nowPlayingMeta}`}>
              {nowPlaying.artistAndAlbum}
            </span>
            <span className={`${styles.nowPlayingLine} ${styles.nowPlayingTitle}`}>{nowPlaying.title}</span>
          </div>
        </div>

        <div className={styles.transportSelects}>
          <label>
            <span>Repeat</span>
            <select
              value={playback.repeatMode}
              onChange={(event) => {
                onSetRepeatMode(event.target.value as RepeatMode);
              }}
            >
              <option value="off">Off</option>
              <option value="all">All</option>
              <option value="one">One</option>
            </select>
          </label>

          <label>
            <span>Random</span>
            <select
              value={playback.shuffleEnabled ? "on" : "off"}
              onChange={(event) => {
                onSetShuffle(event.target.value === "on");
              }}
            >
              <option value="off">Off</option>
              <option value="on">On</option>
            </select>
          </label>
        </div>
      </div>

      <div className={styles.transportRow}>
        <div className={styles.transportGroup}>
          <button type="button" className={styles.transportButton} aria-label="Previous" title="Previous" onClick={onPrevious}>
            <svg className={styles.transportIcon} viewBox="0 0 16 16" aria-hidden="true" focusable="false">
              <rect x="2" y="3" width="2" height="10" />
              <polygon points="12,3 5,8 12,13" />
            </svg>
          </button>
          <button type="button" className={styles.transportButton} aria-label="Stop" title="Stop" onClick={onStop}>
            <svg className={styles.transportIcon} viewBox="0 0 16 16" aria-hidden="true" focusable="false">
              <rect x="4" y="4" width="8" height="8" />
            </svg>
          </button>
          <button
            type="button"
            className={`${styles.transportButton} ${styles.transportButtonPlayPause}`}
            aria-label={playback.state === "playing" ? "Pause" : "Play"}
            title={playback.state === "playing" ? "Pause" : "Play"}
            onClick={onPlayPause}
          >
            {playback.state === "playing" ? (
              <svg className={styles.transportIcon} viewBox="0 0 16 16" aria-hidden="true" focusable="false">
                <rect x="4" y="3" width="3" height="10" />
                <rect x="9" y="3" width="3" height="10" />
              </svg>
            ) : (
              <svg className={styles.transportIcon} viewBox="0 0 16 16" aria-hidden="true" focusable="false">
                <polygon points="5,3 13,8 5,13" />
              </svg>
            )}
          </button>
          <button type="button" className={styles.transportButton} aria-label="Next" title="Next" onClick={onNext}>
            <svg className={styles.transportIcon} viewBox="0 0 16 16" aria-hidden="true" focusable="false">
              <polygon points="4,3 11,8 4,13" />
              <rect x="12" y="3" width="2" height="10" />
            </svg>
          </button>
        </div>

        <div className={styles.readoutRow}>
          <div className={styles.readoutCell}>
            <span className={styles.readoutLabel}>State</span>
            <span>{playback.state}</span>
          </div>
          <div className={styles.readoutCell}>
            <span className={styles.readoutLabel}>Time</span>
            <span>{formatDuration(playback.currentTimeSec)} / {formatDuration(playback.durationSec)}</span>
          </div>
          <div className={styles.readoutCell}>
            <span className={styles.readoutLabel}>Queue</span>
            <span>{trackReadout}</span>
          </div>
        </div>
      </div>

      <div className={styles.seekRow}>
        <input
          type="range"
          min={0}
          max={Math.max(0, playback.durationSec ?? 0)}
          step={1}
          value={Math.min(playback.currentTimeSec, Math.max(0, playback.durationSec ?? 0))}
          onChange={(event) => {
            onSeekAbsolute(Number(event.target.value));
          }}
        />
        <input
          type="range"
          min={0}
          max={130}
          step={1}
          value={playback.volumePercent}
          aria-label={`Volume ${playback.volumePercent}%`}
          onChange={(event) => {
            onSetVolume(Number(event.target.value));
          }}
        />
      </div>
    </header>
  );
}

export const TopRegion = memo(TopRegionComponent);
