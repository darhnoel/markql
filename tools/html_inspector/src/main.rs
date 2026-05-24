mod app;
mod dom;
mod families;
mod fetch;
mod inspector;
mod renderer;
mod skeleton;
mod ui;

use std::env;
use std::io;

use anyhow::{bail, Context, Result};
use crossterm::event::{self, Event};
use crossterm::terminal::{
    disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen,
};
use crossterm::{execute, ExecutableCommand};
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;

use crate::app::App;
use crate::dom::DomTree;
use crate::families::{print_family_report, print_family_report_compact};
use crate::fetch::load_input;
use crate::skeleton::print_skeleton;

const USAGE: &str = "usage: cargo run -- <path-or-url>\n       cargo run -- --skeleton <path-or-url>\n       cargo run -- --families <path-or-url>\n       cargo run -- --families-compact <path-or-url>";

fn main() -> Result<()> {
    match parse_args()? {
        Command::Inspect(input) => {
            let html = load_input(&input)?;
            let dom = DomTree::parse(&html)?;
            run_tui(App::new(input, dom))
        }
        Command::Skeleton(input) => {
            let html = load_input(&input)?;
            let dom = DomTree::parse(&html)?;
            print_skeleton(&dom);
            Ok(())
        }
        Command::Families(input) => {
            let html = load_input(&input)?;
            let dom = DomTree::parse(&html)?;
            print_family_report(&dom);
            Ok(())
        }
        Command::FamiliesCompact(input) => {
            let html = load_input(&input)?;
            let dom = DomTree::parse(&html)?;
            print_family_report_compact(&dom);
            Ok(())
        }
    }
}

enum Command {
    Inspect(String),
    Skeleton(String),
    Families(String),
    FamiliesCompact(String),
}

fn parse_args() -> Result<Command> {
    let mut args = env::args().skip(1);
    let first = args.next().context(USAGE)?;

    if first == "--skeleton" {
        let input = args.next().context(USAGE)?;
        if args.next().is_some() {
            bail!("{USAGE}");
        }
        return Ok(Command::Skeleton(input));
    }

    if first == "--families" {
        let input = args.next().context(USAGE)?;
        if args.next().is_some() {
            bail!("{USAGE}");
        }
        return Ok(Command::Families(input));
    }

    if first == "--families-compact" {
        let input = args.next().context(USAGE)?;
        if args.next().is_some() {
            bail!("{USAGE}");
        }
        return Ok(Command::FamiliesCompact(input));
    }

    if args.next().is_some() {
        bail!("{USAGE}");
    }

    Ok(Command::Inspect(first))
}

fn run_tui(mut app: App) -> Result<()> {
    enable_raw_mode().context("failed to enable raw mode")?;
    let mut stdout = io::stdout();
    stdout
        .execute(EnterAlternateScreen)
        .context("failed to enter alternate screen")?;
    execute!(stdout, crossterm::event::EnableMouseCapture)
        .context("failed to enable mouse capture")?;

    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend).context("failed to initialize terminal")?;
    terminal.clear().context("failed to clear terminal")?;

    let result = event_loop(&mut terminal, &mut app);
    restore_terminal(&mut terminal)?;
    result
}

fn event_loop(terminal: &mut Terminal<CrosstermBackend<io::Stdout>>, app: &mut App) -> Result<()> {
    loop {
        terminal
            .draw(|frame| ui::draw(frame, app))
            .context("failed to draw terminal frame")?;
        let area = terminal.size().context("failed to read terminal size")?;

        match event::read().context("failed to read terminal event")? {
            Event::Key(key) => {
                if app.handle_key(key) {
                    break;
                }
            }
            Event::Mouse(mouse) => {
                app.handle_mouse(mouse, area.width as usize, area.height as usize);
            }
            Event::Resize(_, _) => {
                app.invalidate_layout();
            }
            _ => {}
        }
    }

    Ok(())
}

fn restore_terminal(terminal: &mut Terminal<CrosstermBackend<io::Stdout>>) -> Result<()> {
    disable_raw_mode().context("failed to disable raw mode")?;
    execute!(
        terminal.backend_mut(),
        LeaveAlternateScreen,
        crossterm::event::DisableMouseCapture
    )
    .context("failed to restore screen")?;
    terminal.show_cursor().context("failed to show cursor")?;
    Ok(())
}
