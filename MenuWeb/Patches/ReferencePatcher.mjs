// Collects edits against the ORIGINAL reference text and applies them in one pass.
// Every edit names an exact span of the original; overlapping spans are rejected, so
// no feature patch can depend on (or silently undo) another patch's output.
export class ReferencePatcher {
  constructor(source) {
    this.source = source;
    this.lines = source.split('\n');
    this.edits = [];
    this.dataMutators = [];
    this.resolved = null;
    this.owner = 'unknown';
  }

  // Replace the single line that starts with `prefix`.
  line(prefix, replacement) {
    const start = this.#lineStart(prefix);
    this.#add(start, start + this.lines[this.#lineIndex(prefix)].length, replacement, `line ${prefix}`);
  }

  // Replace `before` (unique within the line starting with `linePrefix`) by `after`.
  edit(linePrefix, before, after) {
    const index = this.#lineIndex(linePrefix);
    const line = this.lines[index];
    const at = line.indexOf(before);
    if (at < 0 || line.indexOf(before, at + 1) >= 0) throw new Error(`${this.owner}: expected one "${before}" in ${linePrefix}`);
    const start = this.#lineStart(linePrefix) + at;
    this.#add(start, start + before.length, after, `${linePrefix} :: ${before}`);
  }

  // Replace every occurrence of `before` in the whole document; `count` guards against drift.
  text(before, after, count = 1) {
    const starts = [];
    for (let at = this.source.indexOf(before); at >= 0; at = this.source.indexOf(before, at + 1)) starts.push(at);
    if (starts.length !== count) throw new Error(`${this.owner}: expected ${count} "${before}", found ${starts.length}`);
    for (const start of starts) this.#add(start, start + before.length, after, before);
  }

  // Mutate the structured DATA object parsed from the original `const DATA = ...;` line.
  data(mutator) {
    this.dataMutators.push({owner: this.owner, mutator});
  }

  // Runs every DATA mutation once and returns the adapted object.
  resolveData() {
    if (this.resolved) return this.resolved;
    const prefix = 'const DATA = ';
    const data = JSON.parse(this.lines[this.#lineIndex(prefix)].slice(prefix.length).replace(/;$/, ''));
    for (const {owner, mutator} of this.dataMutators) {
      this.owner = owner;
      mutator(data);
    }
    this.owner = 'ReferencePatcher';
    this.line(prefix, `${prefix}${JSON.stringify(data)};`);
    return this.resolved = data;
  }

  // Applies every edit to the original text; returns the adapted document.
  apply() {
    this.resolveData();
    const edits = [...this.edits].sort((a, b) => a.start - b.start);
    for (let i = 1; i < edits.length; i++) {
      if (edits[i].start < edits[i - 1].end)
        throw new Error(`Overlapping edits: ${edits[i - 1].owner} (${edits[i - 1].label}) and ${edits[i].owner} (${edits[i].label})`);
    }
    let output = '', cursor = 0;
    for (const edit of edits) {
      output += this.source.slice(cursor, edit.start) + edit.replacement;
      cursor = edit.end;
    }
    return output + this.source.slice(cursor);
  }

  #add(start, end, replacement, label) {
    this.edits.push({start, end, replacement, label, owner: this.owner});
  }

  #lineIndex(prefix) {
    const hits = this.lines.flatMap((line, index) => line.startsWith(prefix) ? [index] : []);
    if (hits.length !== 1) throw new Error(`${this.owner}: expected one line starting with ${prefix}, found ${hits.length}`);
    return hits[0];
  }

  #lineStart(prefix) {
    const index = this.#lineIndex(prefix);
    return this.lines.slice(0, index).reduce((sum, line) => sum + line.length + 1, 0);
  }
}
