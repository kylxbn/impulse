import { useEffect, useMemo, useState, type JSX } from "react";
import styles from "./AlbumArtWindowView.module.css";

function requestedPathFromSearch(): string | null {
  const params = new URLSearchParams(window.location.search);
  const requestedPath = params.get("path");
  return requestedPath && requestedPath.trim().length > 0 ? requestedPath : null;
}

export function AlbumArtWindowView(): JSX.Element {
  const requestedPath = useMemo(() => requestedPathFromSearch(), []);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [imageDataUrl, setImageDataUrl] = useState<string | null>(null);

  useEffect(() => {
    if (!requestedPath) {
      setLoading(false);
      setError("Missing cover art path.");
      return;
    }

    let cancelled = false;
    setLoading(true);
    setError(null);
    setImageDataUrl(null);

    void window.impulse.readCoverArtAsDataUrl(requestedPath).then((dataUrl) => {
      if (cancelled) {
        return;
      }

      if (!dataUrl) {
        setError("Unable to load album art.");
        setLoading(false);
        return;
      }

      setImageDataUrl(dataUrl);
      setLoading(false);
    }).catch((loadError: unknown) => {
      if (cancelled) {
        return;
      }

      setError((loadError as Error).message || "Unable to load album art.");
      setLoading(false);
    });

    return () => {
      cancelled = true;
    };
  }, [requestedPath]);

  return (
    <div className={styles.albumArtWindow}>
      {loading ? <p className={styles.message}>Loading album art...</p> : null}
      {!loading && error ? <p className={styles.error}>{error}</p> : null}
      {!loading && !error && imageDataUrl ? (
        <img
          className={styles.albumArtImage}
          src={imageDataUrl}
          alt="Album art"
          draggable={false}
        />
      ) : null}
    </div>
  );
}
