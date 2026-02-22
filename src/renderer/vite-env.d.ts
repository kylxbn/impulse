/// <reference types="vite/client" />

import type { ImpulseAPI } from "../preload/index";

declare global {
  interface Window {
    impulse: ImpulseAPI;
  }
}
