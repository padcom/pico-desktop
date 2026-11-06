/** Where the CLI's output goes (the terminal, or a test). */
export interface Output {
  out(text: string): void
  err(text: string): void
}
