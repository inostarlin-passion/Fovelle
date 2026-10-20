from pathlib import Path
import json,shlex,subprocess
root=Path('/Users/inostarlin/code/Fovelle');out=root/'reports/evidence/menu-lifecycle/startup-attribution';tmp=Path('/tmp/fovelle-startup-attribution');tmp.mkdir(exist_ok=True)
source=(root/'src/mainwindow.cpp').read_text()
source=source.replace('''    // A visible window supplies its own native menu bar.''','''    QElapsedTimer menuProfile;
    menuProfile.start();
    const auto menuMark = [&menuProfile](const char *phase) {
        qInfo().noquote() << QString("FOVELLE_MAINWINDOW phase=%1 elapsed_ms=%2")
            .arg(phase).arg(menuProfile.nsecsElapsed()/1000000.0, 0, 'f', 3);
    };
    menuMark("menu-build-start");
    // A visible window supplies its own native menu bar.''',1)
source=source.replace('''    mainMenuBar = actionManager.buildMenuBar(this);
    setMenuBar(mainMenuBar);''','''    mainMenuBar = actionManager.buildMenuBar(this);
    menuMark("menu-tree-built");
    setMenuBar(mainMenuBar);
    menuMark("menu-attached");''',1)
source=source.replace('''    menusInitialized = true;
    syncMenuState();''','''    menuMark("menu-virtual-built");
    menusInitialized = true;
    syncMenuState();
    menuMark("menu-synced");''',1)
source=source.replace('''            if (!menusInitialized)
                QTimer::singleShot(0, this, &MainWindow::ensureMenus);''','''            if (qEnvironmentVariableIsSet("FOVELLE_STARTUP_PERF"))
                qInfo().noquote() << "FOVELLE_MAINWINDOW phase=menu-first-queued-turn";
            if (!menusInitialized)
                QTimer::singleShot(0, this, &MainWindow::ensureMenus);''',1)

entries=json.loads((root/'build/compile_commands.json').read_text())
entry=next(x for x in entries if x['file']==str(root/'src/mainwindow.cpp') and x['output'].startswith('CMakeFiles/Fovelle.dir/'))
link=shlex.split((root/'build/CMakeFiles/Fovelle.dir/link.txt').read_text())
for mode in ['menu-profile','eager-menu']:
 s=source
 if mode=='eager-menu':
  s=s.replace('''    markConstruction("constructor-complete");

}''','''    // Causality experiment only: identical menu work before native show.
    if (deferMenus)
        ensureMenus();
    markConstruction("constructor-complete");

}''',1)
 path=tmp/f'mainwindow-{mode}.cpp';path.write_text(s);obj=tmp/f'mainwindow-{mode}.o'
 cmd=shlex.split(entry['command']);cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-c')+1]=str(path)
 cmd=[('-DVERSION_STRING="1.3.3"' if x.startswith('-DVERSION_STRING=') else x) for x in cmd]
 with (out/f'{mode}-build.log').open('w') as log:
  subprocess.run(cmd,cwd=entry['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
  argv=[str(obj) if x=='CMakeFiles/Fovelle.dir/src/mainwindow.cpp.o' else x for x in link]
  argv[argv.index('-o')+1]=f'Fovelle.app/Contents/MacOS/Fovelle-{mode}'
  subprocess.run(argv,cwd=root/'build',stdout=log,stderr=subprocess.STDOUT,check=True)
 print(mode,'built',flush=True)
