declare module "vscode" {
  const vscode: any;
  export = vscode;
}

declare module "node:child_process" {
  export function spawn(command: string, args?: string[], options?: any): any;
}

declare module "node:fs/promises" {
  export function access(path: string): Promise<void>;
}

declare module "node:path" {
  export function join(...paths: string[]): string;
  export function dirname(path: string): string;
}
