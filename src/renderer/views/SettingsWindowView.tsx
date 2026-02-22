import { useEffect, useState, type JSX } from "react";
import type { AppSettings } from "../../shared/types";
import styles from "./SettingsWindowView.module.css";
import shellStyles from "./StandaloneWindow.module.css";

function parseNumberInput(raw: string, fallback: number): number {
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? parsed : fallback;
}

export function SettingsWindowView(): JSX.Element {
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [draft, setDraft] = useState<AppSettings | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);

    void window.impulse.settingsGet().then((settings) => {
      if (cancelled) {
        return;
      }
      setDraft(settings);
      setLoading(false);
    }).catch((loadError: unknown) => {
      if (cancelled) {
        return;
      }
      setError((loadError as Error).message || "Unable to load settings.");
      setLoading(false);
    });

    return () => {
      cancelled = true;
    };
  }, []);

  const setField = <K extends keyof AppSettings>(key: K, value: AppSettings[K]): void => {
    setDraft((previous) => previous ? { ...previous, [key]: value } : previous);
  };

  const save = async (): Promise<void> => {
    if (!draft || saving) {
      return;
    }

    setSaving(true);
    setError(null);

    try {
      const result = await window.impulse.settingsSave(draft);
      if (!result.ok) {
        setError(result.error);
        return;
      }
      window.close();
    } catch (saveError) {
      setError((saveError as Error).message || "Failed to save settings.");
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className={shellStyles.standaloneWindow}>
      <header className={shellStyles.standaloneHeader}>
        <h1>Settings</h1>
        <p>Player and library configuration</p>
      </header>

      <div className={shellStyles.standaloneContent}>
        {loading ? <p className={styles.message}>Loading settings...</p> : null}
        {!loading && error ? <p className={styles.error}>{error}</p> : null}

        {!loading && draft ? (
          <div className={styles.contentLayout}>
            <section className={styles.section}>
              <h4>Library</h4>
              <div className={styles.settingsGrid}>
                <label>
                  <span>Music root path</span>
                  <input
                    type="text"
                    value={draft.musicRoot}
                    onChange={(event) => setField("musicRoot", event.target.value)}
                  />
                </label>
              </div>
            </section>

            <section className={styles.section}>
              <h4>ReplayGain</h4>
              <div className={styles.settingsGrid}>
                <label>
                  <span>Tagged preamp (dB)</span>
                  <input
                    type="number"
                    step="0.1"
                    value={draft.replaygainPreampTaggedDb}
                    onChange={(event) =>
                      setField(
                        "replaygainPreampTaggedDb",
                        parseNumberInput(event.target.value, draft.replaygainPreampTaggedDb)
                      )}
                  />
                </label>

                <label>
                  <span>Untagged preamp (dB)</span>
                  <input
                    type="number"
                    step="0.1"
                    value={draft.replaygainPreampUntaggedDb}
                    onChange={(event) =>
                      setField(
                        "replaygainPreampUntaggedDb",
                        parseNumberInput(event.target.value, draft.replaygainPreampUntaggedDb)
                      )}
                  />
                </label>
              </div>
            </section>

            <section className={styles.section}>
              <h4>Analysis</h4>
              <div className={styles.settingsGrid}>
                <label>
                  <span>PLR reference loudness (dB)</span>
                  <input
                    type="number"
                    step="0.1"
                    value={draft.plrReferenceLoudnessDb}
                    onChange={(event) =>
                      setField(
                        "plrReferenceLoudnessDb",
                        parseNumberInput(event.target.value, draft.plrReferenceLoudnessDb)
                      )}
                  />
                </label>

                <label>
                  <span>Volume step (%)</span>
                  <input
                    type="number"
                    min="1"
                    value={draft.volumeStepPercent}
                    onChange={(event) =>
                      setField("volumeStepPercent", parseNumberInput(event.target.value, draft.volumeStepPercent))}
                  />
                </label>
              </div>
            </section>
          </div>
        ) : null}
      </div>

      <footer className={shellStyles.standaloneFooter}>
        <button type="button" onClick={() => window.close()}>Cancel</button>
        <button
          type="button"
          className={shellStyles.primaryButton}
          disabled={!draft || saving}
          onClick={() => void save()}
        >
          {saving ? "Saving..." : "Save"}
        </button>
      </footer>
    </div>
  );
}
