import { useEffect, useMemo, useState, type JSX } from "react";
import type { FilePropertiesSnapshot } from "../../shared/types";
import { FilePropertiesContent } from "../components/FilePropertiesContent";
import styles from "./StandaloneWindow.module.css";

function requestedPathFromSearch(): string | null {
  const params = new URLSearchParams(window.location.search);
  const requestedPath = params.get("path");
  return requestedPath && requestedPath.trim().length > 0 ? requestedPath : null;
}

export function FilePropertiesWindowView(): JSX.Element {
  const requestedPath = useMemo(() => requestedPathFromSearch(), []);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [properties, setProperties] = useState<FilePropertiesSnapshot | null>(null);

  useEffect(() => {
    if (!requestedPath) {
      setLoading(false);
      setError("Missing file path.");
      return;
    }

    let cancelled = false;
    setLoading(true);
    setError(null);
    setProperties(null);

    void window.impulse.getFileProperties(requestedPath).then((payload) => {
      if (cancelled) {
        return;
      }
      setProperties(payload);
      setLoading(false);
    }).catch((loadError: unknown) => {
      if (cancelled) {
        return;
      }
      setError((loadError as Error).message || "Unable to load file properties.");
      setLoading(false);
    });

    return () => {
      cancelled = true;
    };
  }, [requestedPath]);

  return (
    <div className={styles.standaloneWindow}>
      <header className={styles.standaloneHeader}>
        <h1>File Properties</h1>
        <p>{requestedPath ?? "No file selected"}</p>
      </header>

      <div className={styles.standaloneContent}>
        <FilePropertiesContent
          loading={loading}
          requestedPath={requestedPath}
          properties={properties}
          error={error}
        />
      </div>

      <footer className={styles.standaloneFooter}>
        <button type="button" className={styles.primaryButton} onClick={() => window.close()}>
          Close
        </button>
      </footer>
    </div>
  );
}
