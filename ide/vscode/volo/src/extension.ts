import * as path from 'path';
import * as fs from 'fs';
import { ExtensionContext, workspace, WorkspaceFolder } from 'vscode';

import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

function getWorkspaceServerPaths(workspace: WorkspaceFolder): string[] {
  const serverPath = path.join(workspace.uri.fsPath, "build", "apps", "utilities", "app_lsp");
  return [serverPath, serverPath + ".exe"];
}

function getServerPaths(): string[] {
  return workspace.workspaceFolders.flatMap(getWorkspaceServerPaths);
}

function getValidServerPath(): string | undefined {
  return getServerPaths().filter(fs.existsSync).pop();
}

export function activate(context: ExtensionContext) {
  const serverPath: string | undefined = getValidServerPath();
  if (serverPath === undefined) {
    throw Error("No app_lsp binary found in workspace, did you build the project?");
  }

  const serverOptions: ServerOptions = {
    command: serverPath,
    transport: TransportKind.stdio,
    args: [],
    options: {}
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'volo-script' }],
    stdioEncoding: "utf8",
    synchronize: {}
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
