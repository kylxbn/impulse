import React from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import { AlbumArtWindowView } from "./views/AlbumArtWindowView";
import { FilePropertiesWindowView } from "./views/FilePropertiesWindowView";
import { SettingsWindowView } from "./views/SettingsWindowView";
import "./styles.css";

const container = document.getElementById("root");
if (!container) {
  throw new Error("Missing #root container");
}

const params = new URLSearchParams(window.location.search);
const view = params.get("view");

const RootComponent = (() => {
  if (view === "settings") {
    return SettingsWindowView;
  }

  if (view === "file-properties") {
    return FilePropertiesWindowView;
  }

  if (view === "album-art") {
    return AlbumArtWindowView;
  }

  return App;
})();

createRoot(container).render(
  <React.StrictMode>
    <RootComponent />
  </React.StrictMode>
);
