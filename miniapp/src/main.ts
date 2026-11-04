/**
 * The page: the span of time this visit has lasted, from when the page opened until now. It asks the app only where
 * it runs (watchEnv: language, look, the room under the capsule), so it declares no capabilities and no call can fail.
 */
import { watchEnv, type MiniappEnv } from "@tikpal/miniapp";
import { words } from "./i18n.js";
import "./app.css";

const root = document.getElementById("root")!;
const openedAt = new Date();
let env: MiniappEnv | undefined;
let shown = "";

function el<K extends keyof HTMLElementTagNameMap>(tag: K, text: string, className?: string) {
  const node = document.createElement(tag);
  node.textContent = text;
  if (className) node.className = className;
  return node;
}

function render() {
  if (!env) return;
  const w = words(env.locale);
  const now = new Date();
  const clock = new Intl.DateTimeFormat(env.locale, { hour: "2-digit", minute: "2-digit", hourCycle: "h23" });
  const span = `${clock.format(openedAt)} – ${clock.format(now)}`;
  const used = w.used(Math.floor((now.getTime() - openedAt.getTime()) / 60000));
  // Called every second: the page changes only when what it says does.
  if (`${env.locale}|${span}|${used}` === shown) return;
  shown = `${env.locale}|${span}|${used}`;
  root.replaceChildren(el("h1", w.title), el("p", w.label, "label"), el("p", span, "span"), el("p", used, "used"));
}

// No top-level await: Android's system WebView can be as old as Chrome 87, which has none.
void watchEnv((next) => {
  env = next;
  shown = "";
  render();
}).then((first) => {
  env = first;
  render();
  setInterval(render, 1000);
});
