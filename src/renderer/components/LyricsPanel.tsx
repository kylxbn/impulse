import type { JSX } from "react";
import type { LyricsSnapshot } from "../../shared/types";
import styles from "./LyricsPanel.module.css";

interface LyricsPanelProps {
  lyrics: LyricsSnapshot;
}

export function LyricsPanel({ lyrics }: LyricsPanelProps): JSX.Element | null {
  if (!lyrics.visible && !lyrics.error) {
    return null;
  }

  const hasActiveLine = lyrics.activeIndex >= 0 && lyrics.activeIndex < lyrics.lines.length;
  const previousLine = hasActiveLine && lyrics.activeIndex > 0 ? lyrics.lines[lyrics.activeIndex - 1]?.text ?? "" : "";
  const currentLine = hasActiveLine ? lyrics.lines[lyrics.activeIndex]?.text ?? "" : "";
  const nextLine = hasActiveLine ? lyrics.lines[lyrics.activeIndex + 1]?.text ?? "" : lyrics.lines[0]?.text ?? "";

  return (
    <section className={styles.lyricsPanel}>
      {lyrics.error ? <div className={styles.lyricsError}>{lyrics.error}</div> : null}
      {!lyrics.error ? (
        <>
          <div className={`${styles.lyricsLine} ${styles.lyricsLineDim}`}>{previousLine}</div>
          <div className={`${styles.lyricsLine} ${styles.lyricsLineCurrent}`}>{currentLine || "..."}</div>
          <div className={`${styles.lyricsLine} ${styles.lyricsLineDim}`}>{nextLine}</div>
        </>
      ) : null}
    </section>
  );
}
