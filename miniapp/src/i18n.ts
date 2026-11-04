/** The page's words in the app's four languages (env.locale). Add a key to all four. */
import type { MiniappEnv } from "@tikpal/miniapp";

const WORDS = {
  en: {
    title: "Night Light",
    label: "This visit",
    used: (minutes: number) => (minutes < 1 ? "Under a minute so far" : `${minutes} min so far`),
  },
  "zh-Hans": {
    title: "小夜灯",
    label: "这次打开的时间段",
    used: (minutes: number) => (minutes < 1 ? "还不到 1 分钟" : `已用 ${minutes} 分钟`),
  },
  ja: {
    title: "ナイトライト",
    label: "今回開いている時間",
    used: (minutes: number) => (minutes < 1 ? "まだ 1 分未満" : `${minutes} 分経過`),
  },
  "es-419": {
    title: "Luz nocturna",
    label: "Esta visita",
    used: (minutes: number) => (minutes < 1 ? "Menos de un minuto" : `${minutes} min hasta ahora`),
  },
} satisfies Record<MiniappEnv["locale"], unknown>;

export const words = (locale: MiniappEnv["locale"]) => WORDS[locale] ?? WORDS.en;
