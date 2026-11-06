// A same-origin subframe reports whether it got the bridge (it mustn't, by default).
parent.postMessage({ frame: 'same-origin', bridge: typeof window.picoDesktop }, '*')
