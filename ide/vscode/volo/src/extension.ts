import * as path from "path";
import * as fs from "fs";
import { ExtensionContext, workspace, WorkspaceFolder } from "vscode";

import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient;

function getWorkspaceServerPaths(workspaceFolder: WorkspaceFolder): string[] {
  const serverPath = path.join(workspaceFolder.uri.fsPath, "bin", "lsp");
  return [serverPath, serverPath + ".exe"];
}

function getServerPaths(): string[] {
  return workspace.workspaceFolders.flatMap(getWorkspaceServerPaths);
}

function getValidServerPath(): string | undefined {
  return getServerPaths().filter(fs.existsSync)[0];
}

function getBinderDirectoryPath(): string | undefined {
  return workspace.workspaceFolders
    .map((workspaceFolder) => path.join(workspaceFolder.uri.fsPath, "assets", "schemas"))
    .filter(fs.existsSync)[0];
}

export function activate(context: ExtensionContext) {
  const serverPath: string | undefined = getValidServerPath();
  if (serverPath === undefined) {
    throw Error("No bin/lsp binary found in workspace");
  }

  let serverArgs: string[] = [];

  const binderPath: string | undefined = getBinderDirectoryPath();
  if (binderPath !== undefined) {
    serverArgs.push("--binders", `${binderPath}/script_*_binder.json`);
  }

  const serverOptions: ServerOptions = {
    command: serverPath,
    transport: TransportKind.stdio,
    args: serverArgs,
    options: {},
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "volo-script" }],
    stdioEncoding: "utf8",
    initializationOptions: {
      profile: workspace.getConfiguration("volo-lsp").get("profile"),
    },
    synchronize: {},
  };

  client = new LanguageClient('volo-lsp', 'Volo Script', serverOptions, clientOptions);
  client.start();
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
