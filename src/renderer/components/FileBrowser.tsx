import { memo, useMemo } from "react";
import type { DragEvent, MouseEvent, JSX } from "react";
import type { FileBrowserNode } from "../../shared/types";
import styles from "./FileBrowser.module.css";

export const BROWSER_DRAG_MIME = "application/x-impulse-browser-paths";

interface FileBrowserContextPayload {
  x: number;
  y: number;
  targetPath: string;
  selectedPaths: string[];
}

interface FileBrowserProps {
  rootPath: string;
  selectedPaths: string[];
  selectionAnchorPath: string | null;
  entriesByPath: Record<string, FileBrowserNode[]>;
  expandedPaths: Set<string>;
  loadingPaths: Set<string>;
  onSelectionChange: (paths: string[], anchorPath: string | null) => void;
  onToggle: (path: string) => void;
  onActivate: (path: string) => void;
  onContextMenu: (payload: FileBrowserContextPayload) => void;
}

interface SelectionOptions {
  targetPath: string;
  shiftKey: boolean;
  toggleKey: boolean;
  preserveIfSelected: boolean;
}

function FileBrowserComponent({
  rootPath,
  selectedPaths,
  selectionAnchorPath,
  entriesByPath,
  expandedPaths,
  loadingPaths,
  onSelectionChange,
  onToggle,
  onActivate,
  onContextMenu
}: FileBrowserProps): JSX.Element {
  const rootEntries = entriesByPath[rootPath] ?? [];

  const visiblePaths = useMemo(() => {
    const ordered: string[] = [];

    const walk = (node: FileBrowserNode): void => {
      ordered.push(node.path);
      if (node.type !== "directory" || !expandedPaths.has(node.path)) {
        return;
      }

      const children = entriesByPath[node.path] ?? [];
      for (const child of children) {
        walk(child);
      }
    };

    for (const child of rootEntries) {
      walk(child);
    }

    return ordered;
  }, [entriesByPath, expandedPaths, rootEntries]);

  const orderSelection = (paths: string[]): string[] => {
    const selectedSet = new Set(paths);
    return visiblePaths.filter((path) => selectedSet.has(path));
  };

  const applySelection = ({
    targetPath,
    shiftKey,
    toggleKey,
    preserveIfSelected
  }: SelectionOptions): string[] => {
    const isAlreadySelected = selectedPaths.includes(targetPath);

    if (preserveIfSelected && isAlreadySelected) {
      return selectedPaths;
    }

    if (shiftKey && selectionAnchorPath) {
      const anchorIndex = visiblePaths.indexOf(selectionAnchorPath);
      const targetIndex = visiblePaths.indexOf(targetPath);
      if (anchorIndex !== -1 && targetIndex !== -1) {
        const [start, end] = anchorIndex <= targetIndex
          ? [anchorIndex, targetIndex]
          : [targetIndex, anchorIndex];
        const range = visiblePaths.slice(start, end + 1);
        onSelectionChange(range, selectionAnchorPath);
        return range;
      }
    }

    if (toggleKey) {
      const next = isAlreadySelected
        ? selectedPaths.filter((path) => path !== targetPath)
        : orderSelection([...selectedPaths, targetPath]);
      const nextAnchor = next.includes(targetPath)
        ? targetPath
        : next[next.length - 1] ?? null;
      onSelectionChange(next, nextAnchor);
      return next;
    }

    const next = [targetPath];
    onSelectionChange(next, targetPath);
    return next;
  };

  const handleDragStart = (
    event: DragEvent<HTMLElement>,
    targetPath: string
  ): void => {
    const currentSelection = selectedPaths.includes(targetPath)
      ? selectedPaths
      : [targetPath];

    if (!selectedPaths.includes(targetPath)) {
      onSelectionChange([targetPath], targetPath);
    }

    event.dataTransfer.setData(BROWSER_DRAG_MIME, JSON.stringify(currentSelection));
    event.dataTransfer.setData("text/plain", currentSelection.join("\n"));
    event.dataTransfer.effectAllowed = "copy";
  };

  const handleRowClick = (
    event: MouseEvent<HTMLElement>,
    targetPath: string
  ): void => {
    applySelection({
      targetPath,
      shiftKey: event.shiftKey,
      toggleKey: event.ctrlKey || event.metaKey,
      preserveIfSelected: false
    });
  };

  const handleRowContextMenu = (
    event: MouseEvent<HTMLElement>,
    targetPath: string
  ): void => {
    event.preventDefault();
    const nextSelection = applySelection({
      targetPath,
      shiftKey: false,
      toggleKey: false,
      preserveIfSelected: true
    });

    onContextMenu({
      x: event.clientX,
      y: event.clientY,
      targetPath,
      selectedPaths: nextSelection
    });
  };

  const renderLoadingRow = (key: string, depth: number): JSX.Element => (
    <div
      key={key}
      className={`${styles.treeRow} ${styles.treeRowLoading}`}
      style={{ paddingLeft: `${depth * 10 + 10}px` }}
    >
      <span className={styles.treeSpacer}>·</span>
      <span className={`${styles.treeName} ${styles.treeNameLoading}`}>Loading...</span>
    </div>
  );

  const renderNode = (node: FileBrowserNode, depth: number): JSX.Element[] => {
    const isExpanded = node.type === "directory" && expandedPaths.has(node.path);
    const isSelected = selectedPaths.includes(node.path);

    const row = (
      <div
        key={node.path}
        className={`${styles.treeRow} ${isSelected ? styles.treeRowSelected : ""}`.trim()}
        style={{ paddingLeft: `${depth * 10 + 10}px` }}
        onClick={(event) => {
          handleRowClick(event, node.path);
        }}
        onContextMenu={(event) => {
          handleRowContextMenu(event, node.path);
        }}
        onDoubleClick={() => {
          if (node.type === "directory") {
            onToggle(node.path);
          } else {
            onActivate(node.path);
          }
        }}
        draggable
        onDragStart={(event) => {
          handleDragStart(event, node.path);
        }}
      >
        {node.type === "directory" ? (
          <button
            className={styles.treeToggle}
            type="button"
            aria-label={isExpanded ? "Collapse directory" : "Expand directory"}
            onClick={(event) => {
              event.stopPropagation();
              onToggle(node.path);
            }}
          >
            {isExpanded ? (
              <svg className={styles.treeChevron} viewBox="0 0 12 12" aria-hidden="true" focusable="false">
                <polyline points="2,4 6,8 10,4" />
              </svg>
            ) : (
              <svg className={styles.treeChevron} viewBox="0 0 12 12" aria-hidden="true" focusable="false">
                <polyline points="4,2 8,6 4,10" />
              </svg>
            )}
          </button>
        ) : (
          <span className={styles.treeSpacer}>·</span>
        )}
        <span className={`${styles.treeName} ${node.type === "directory" ? styles.treeNameDir : styles.treeNameFile}`}>
          {node.name}
        </span>
      </div>
    );

    if (!isExpanded) {
      return [row];
    }

    const children = entriesByPath[node.path] ?? [];
    const loadingRow = loadingPaths.has(node.path)
      ? [renderLoadingRow(`${node.path}:loading`, depth + 1)]
      : [];
    const childRows = children.flatMap((child) => renderNode(child, depth + 1));
    return [row, ...loadingRow, ...childRows];
  };

  const renderedRows = rootEntries.flatMap((node) => renderNode(node, 0));
  if (renderedRows.length === 0 && loadingPaths.has(rootPath)) {
    renderedRows.push(renderLoadingRow(`${rootPath}:loading`, 0));
  }

  return (
    <section className={`${styles.panel} ${styles.fileBrowser}`}>
      <header className={styles.panelHeader}>
        <h2>Files</h2>
        <small>{rootPath}</small>
      </header>
      <div className={styles.treeScroll}>
        {renderedRows}
      </div>
    </section>
  );
}

export const FileBrowser = memo(FileBrowserComponent);
