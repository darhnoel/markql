import * as vscode from "vscode";
import { registerExtension } from "./registration";

export function activate(context: any): void {
  registerExtension(vscode, context);
}

export function deactivate(): void {}
