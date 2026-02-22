import { useEffect, useState, type JSX } from "react";
import type { AppSettings } from "../../shared/types";
import styles from "./SettingsModal.module.css";

interface SettingsModalProps {
  open: boolean;
  settings: AppSettings;
  onSave: (next: AppSettings) => Promise<void>;
  onClose: () => void;
}

export function SettingsModal({ open, settings, onSave, onClose }: SettingsModalProps): JSX.Element | null {
  const [draft, setDraft] = useState<AppSettings>(settings);

  useEffect(() => {
    setDraft(settings);
  }, [settings]);

  if (!open) {
    return null;
  }

  const setField = <K extends keyof AppSettings>(key: K, value: AppSettings[K]): void => {
    setDraft((prev) => ({ ...prev, [key]: value }));
  };

  const onSubmit = async (): Promise<void> => {
    await onSave(draft);
  };

  return (
    <div className={styles.modalBackdrop} role="presentation" onClick={onClose}>
      <div className={styles.modal} role="dialog" aria-modal="true" onClick={(event) => event.stopPropagation()}>
        <header className={styles.modalHeader}>
          <h3>Settings</h3>
        </header>

        <div className={styles.modalGrid}>
          <label>
            <span>Music root path</span>
            <input
              type="text"
              value={draft.musicRoot}
              onChange={(event) => setField("musicRoot", event.target.value)}
            />
          </label>

          <label>
            <span>ReplayGain tagged preamp (dB)</span>
            <input
              type="number"
              step="0.1"
              value={draft.replaygainPreampTaggedDb}
              onChange={(event) => setField("replaygainPreampTaggedDb", Number(event.target.value))}
            />
          </label>

          <label>
            <span>ReplayGain untagged preamp (dB)</span>
            <input
              type="number"
              step="0.1"
              value={draft.replaygainPreampUntaggedDb}
              onChange={(event) => setField("replaygainPreampUntaggedDb", Number(event.target.value))}
            />
          </label>

          <label>
            <span>PLR reference loudness (dB)</span>
            <input
              type="number"
              step="0.1"
              value={draft.plrReferenceLoudnessDb}
              onChange={(event) => setField("plrReferenceLoudnessDb", Number(event.target.value))}
            />
          </label>

          <label>
            <span>Volume step (%)</span>
            <input
              type="number"
              min="1"
              value={draft.volumeStepPercent}
              onChange={(event) => setField("volumeStepPercent", Number(event.target.value))}
            />
          </label>
        </div>

        <footer className={styles.modalFooter}>
          <button type="button" onClick={onClose}>Cancel</button>
          <button type="button" className={styles.primaryButton} onClick={() => void onSubmit()}>
            Save
          </button>
        </footer>
      </div>
    </div>
  );
}
