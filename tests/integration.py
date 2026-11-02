#!/usr/bin/env python3
"""Exercise the CLI, real terminal input, persisted results, and staged installs."""
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time
from terminal_screen import Screen

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/fvoci'
DEMO = ROOT / 'vocab/demo.json'

class Terminal:
    def __init__(self, args, directory):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 110, 0, 0))
        env = dict(os.environ, TERM='xterm-256color', LANG='C.UTF-8')
        self.proc = subprocess.Popen([str(BIN), '--config', str(directory / 'config.json'), *args], stdin=slave, stdout=slave, stderr=slave, env=env, cwd=ROOT)
        os.close(slave)
        self.output = b''
        self.screen = Screen()
    def send(self, data):
        os.write(self.master, data.encode() if isinstance(data, str) else data)
    def wait(self, text, timeout=5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in self.screen.text:
                return
            ready, _, _ = select.select([self.master], [], [], .05)
            if ready:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                self.output += chunk
                self.screen.feed(chunk)
        raise AssertionError(f'TUI did not show {text!r}; screen:\n{self.screen.text}\nraw tail={self.output[-1000:]!r}')
    def capture(self, name):
        destination = os.environ.get('FVOCI_UI_CAPTURE_DIR')
        if destination:
            directory = Path(destination)
            directory.mkdir(parents=True, exist_ok=True)
            (directory / (name + '.json')).write_text(json.dumps(self.screen.cells, ensure_ascii=False))
    def resize(self, rows, cols):
        self.screen.resize(rows, cols)
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        self.proc.send_signal(signal.SIGWINCH)
    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(timeout=3)
        os.close(self.master)


def run(*args, code=0):
    p = subprocess.run([str(BIN), *map(str, args)], cwd=ROOT, capture_output=True, text=True)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    return p

with tempfile.TemporaryDirectory(prefix='fvoci-integration-') as folder:
    tmp = Path(folder)
    run('--validate', DEMO)
    answer = tmp / 'answers.json'
    answer.write_text(json.dumps([{'text': 'le/les chat.s(m)'}]))
    grade = json.loads(run('--vocab', DEMO, '--entry', 'cat', '--grade', answer).stdout)
    assert grade['raw_correct'] and set(grade['expanded_units']) == {'chat.m_sg', 'chat.m_pl'} and not grade['inferred_units']
    run('--vocab', DEMO, '--entry', 'cat', '--grade', answer, '--no-expansion', code=1)
    malformed = tmp / 'bad.json'
    malformed.write_text('{"schema_version": 99}')
    run('--validate', malformed, code=2)
    run('--questions', '-1', code=2)

    profile = tmp / 'profile.json'
    tui = Terminal(['--vocab', str(DEMO), '--progress', str(profile), '--mode', 'text', '--direction', 'de_fr', '--questions', '2'], tmp)
    try:
        tui.wait('New progress profile')
        tui.wait('Accuracy: 0.0%')
        tui.capture('start')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.wait('Full sweep (12)')
        tui.wait('Difficult sweep (0)')
        tui.wait('Very difficult sweep (0)')
        tui.capture('learn-menu')
        tui.send('jj\n')
        tui.wait('No matching words')
        tui.send('\n')
        tui.wait('Default (Full sweep)')
        tui.send('\n')
        tui.wait('die Katze')
        tui.wait('Insert')
        tui.wait('╰')
        tui.capture('written-question')
        prompt_line = next(line for line in tui.screen.lines if 'die Katze' in line)
        assert abs(prompt_line.index('die Katze') * 2 + len('die Katze') - tui.screen.columns) <= 1
        assert set(tui.screen.colors_of('Translate into French:')) == {None}
        assert set(tui.screen.colors_of('Accuracy: 0.0%')) == {None}
        assert not any(label in tui.screen.text for label in
                       ('Written recall', 'new word', 'required word', 'Separate forms'))
        question_row = next(i for i, line in enumerate(tui.screen.lines) if 'die Katze' in line)
        answer_row = next(i for i, line in enumerate(tui.screen.lines) if '│' in line)
        tui.send('le chaz')
        tui.send('\x1b')
        tui.wait('Normal')
        assert tui.screen.cursor_visible and tui.screen.cursor_shape == 2
        tui.send('xa')
        submitted_at = time.monotonic()
        tui.send('t, les chats (m)\n')
        tui.wait('Correct')
        tui.wait('Next word…')
        tui.wait('Accuracy: 8.3%')
        assert set(tui.screen.colors_of('Correct — well done')) == {(80, 216, 144)}
        assert set(tui.screen.colors_of('le chat')) == {(80, 216, 144)}
        tui.capture('written-correct')
        assert 'die Katze' in tui.screen.lines[question_row]
        assert tui.screen.lines[answer_row].startswith('│')
        assert tui.screen.lines[answer_row].endswith('│')
        assert 'One sufficient answer' in tui.screen.text
        assert 'les chats' in tui.screen.text
        assert 'le chat (m)' not in tui.screen.text
        assert b'38:2::80:216:144m' in tui.output
        tui.wait('das Auge')
        assert .12 <= time.monotonic() - submitted_at < 1.5
        tui.resize(24, 60)
        tui.send("l'oe+il (m), les yeux (m)\n")
        tui.wait('Correct')
        tui.wait('Accuracy: 16.7%')
        tui.capture('written-small-terminal')
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    progress = json.loads(profile.read_text())
    attempts = [e for e in progress['history'] if e.get('event') == 'attempt']
    assert len(attempts) == 2 and all(e['raw_correct'] for e in attempts)
    assert attempts[0]['rows'][0]['text'] == 'le chat'
    assert attempts[1]['rows'][0]['text'] == "l'œil"
    assert attempts[1]['rows'][1]['text'] == 'les yeux'
    assert profile.with_suffix('.json.bak').exists()

    # Exercise the new notation through real keyboard input, including e+ and
    # longest-match oe+, malformed syntax feedback, and saved expansion evidence.
    expansion_source = json.loads((ROOT / 'vocab/lecon-6.json').read_text())
    expansion_source['id'] = 'expansion-terminal-test'
    wanted = ['disabled-person', 'treat', 'eye']
    expansion_source['entries'] = [next(e for e in expansion_source['entries'] if e['id'] == name) for name in wanted]
    for e in expansion_source['entries']:
        e.pop('related_entry_ids', None)
    expansion_deck = tmp / 'expansion.json'
    expansion_deck.write_text(json.dumps(expansion_source, ensure_ascii=False))
    expansion_profile = tmp / 'expansion-progress.json'
    tui = Terminal(['--vocab', str(expansion_deck), '--progress', str(expansion_profile),
                    '--mode', 'text', '--direction', 'de_fr', '--questions', '3'], tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('die Behinderte')
        tui.send('le/la handicape+.e\n')
        tui.wait('Correct — well done')
        tui.wait('Accuracy: 33.3%')
        tui.capture('written-expansion-correct')
        tui.wait('behandeln')
        tui.send('traiter / soigner\n')
        tui.wait('Incorrect — review')
        assert tui.proc.poll() is None
        tui.send('\n')
        tui.wait('das Auge')
        tui.send("{l'oe+il,les yeux}(m)\n")
        tui.wait('Correct — well done')
        tui.wait('Accuracy: 66.7%')
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    expansion_progress = json.loads(expansion_profile.read_text())
    events = expansion_progress['history']
    assert [event['raw_correct'] for event in events] == [True, False, True]
    assert events[0]['rows'][0]['text'] == 'le/la handicapé.e'
    assert [r['text'] for r in events[0]['expanded_answers']] == ['le handicapé', 'la handicapée']
    assert len(events[0]['expanded_units']) == 2 and not events[0]['inferred_units']
    production = next(s for s in expansion_progress['schedules']
                      if s['entry_id'] == 'disabled-person' and s['direction'] == 'de_fr')
    assert len(production['units']) == 2
    assert all(unit['expanded'] == 1 and unit['attempts'] == 1 for unit in production['units'].values())
    assert events[1]['errors'][0]['kind'] == 'expansion'
    assert len(events[2]['expanded_units']) == 2
    # Legacy config upgrades to text expansion, while retained history still
    # belongs to the old grading policy and is not misreported as new success.
    legacy_config = tmp / 'legacy-settings.json'
    legacy_config.write_text(json.dumps({'inference': 'naive-v1'}))
    upgraded = json.loads(run('--config', legacy_config, '--dump-config').stdout)
    assert upgraded['inference'] == 'text-expansion-v1'

    # The sentence override must be one attempt with preserved raw mismatch.
    source = json.loads(DEMO.read_text())
    source['id'] = 'sentence-test'
    source['entries'] = [e for e in source['entries'] if e['id'] == 'feel-unwell']
    sentence = tmp / 'sentence.json'
    sentence.write_text(json.dumps(source, ensure_ascii=False))
    sentence_profile = tmp / 'sentence-progress.json'
    tui = Terminal(['--vocab', str(sentence), '--progress', str(sentence_profile), '--mode', 'text', '--direction', 'de_fr', '--questions', '1'], tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('Ich')
        tui.send('Je ne me sens pas bienn.\n')
        tui.wait('Incorrect — review')
        tui.wait('I was correct')
        assert set(tui.screen.colors_of('Incorrect — review')) == {(255, 100, 100)}
        assert set(tui.screen.colors_of('Je ne me sens pas bienn.')) == {(255, 100, 100)}
        tui.capture('written-incorrect')
        time.sleep(.3)  # Mistakes remain visible until explicitly acknowledged.
        tui.send('c')
        tui.wait('Accepted by you')
        tui.wait('Accuracy: 100.0%')
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    history = json.loads(sentence_profile.read_text())['history']
    assert len(history) == 1 and history[0]['self_override'] and not history[0]['raw_correct'] and history[0]['effective_correct']

    # Save an unfinished answer, reopen it, and continue the same answer bar.
    source['id'] = 'resume-test'
    source['entries'] = [e for e in json.loads(DEMO.read_text())['entries'] if e['id'] == 'eye']
    resume_deck = tmp / 'resume.json'
    resume_deck.write_text(json.dumps(source, ensure_ascii=False))
    resume_profile = tmp / 'resume-progress.json'
    args = ['--vocab', str(resume_deck), '--progress', str(resume_profile), '--mode', 'text', '--direction', 'de_fr', '--questions', '1']
    tui = Terminal(args, tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('das Auge')
        tui.send("l'oe+il (m)")
        tui.send('\x1b')
        tui.wait('Normal')
        tui.send('q')
        tui.wait('Save and exit session')
        tui.send('jjj\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    assert json.loads(resume_profile.read_text())['session']['draft'][0]['text'] == "l'œil (m)"
    tui = Terminal(args, tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('das Auge')
        tui.send(', les yeux (f\x7fm)\n')
        tui.wait('Correct')
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    assert json.loads(resume_profile.read_text())['history'][0]['raw_correct']

    # A single German plural earns full credit; unused forms are alternatives.
    reverse_profile = tmp / 'reverse-progress.json'
    tui = Terminal(['--vocab', str(resume_deck), '--progress', str(reverse_profile),
                    '--mode', 'text', '--direction', 'fr_de', '--questions', '1'], tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('Translate into German:')
        tui.wait('Insert')
        tui.send('DIE, AUGEN!\n')
        tui.wait('Correct — well done')
        tui.wait('Accepted answers (any one)')
        tui.wait('Next word…')
        assert '[needed]' not in tui.screen.text
        assert '[alternative]' in tui.screen.text
        assert 'Accuracy: 100.0%' in tui.screen.text
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    event = json.loads(reverse_profile.read_text())['history'][0]
    assert event['raw_correct'] and not event['missing_units']
    assert event['explicit_units'] == ['plural']

    # Recognition-only sessions store actual options and no invented typed recall.
    choice_profile = tmp / 'choice-progress.json'
    tui = Terminal(['--vocab', str(DEMO), '--progress', str(choice_profile), '--mode', 'multiple_choice', '--direction', 'de_fr', '--questions', '1', '--seed', '7'], tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('Choose one answer')
        state = json.loads(choice_profile.read_text())['session']['pending']
        option = state['choices'].index(state['entry'])
        tui.capture('choice-question')
        tui.send(str(option + 1))
        tui.wait('Correct')
        tui.wait('Your answer — correct')
        assert set(tui.screen.colors_of('Your answer — correct')) == {(80, 216, 144)}
        assert 'Accuracy: 0.0%' in tui.screen.text
        tui.capture('choice-correct')
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    p = json.loads(choice_profile.read_text())
    assert p['history'][0]['raw_correct'] and p['history'][0]['rows'] == []
    assert all(s['stage'] == -1 for s in p['schedules'])

    # Wrong multiple-choice feedback retains the options and explicitly colors both results.
    for color in ('auto', 'never'):
        directory = tmp / ('wrong-choice-' + color)
        directory.mkdir()
        (directory / 'config.json').write_text(json.dumps({'color': color}))
        profile = directory / 'progress.json'
        tui = Terminal(['--vocab', str(DEMO), '--progress', str(profile), '--mode', 'multiple_choice', '--direction', 'de_fr', '--questions', '1', '--seed', '7'], directory)
        try:
            tui.wait('New progress profile')
            tui.send('jjjj\n')
            tui.wait('Default (')
            tui.send('\n')
            tui.wait('Choose one answer')
            state = json.loads(profile.read_text())['session']['pending']
            wrong = next(i for i, word in enumerate(state['choices']) if word != state['entry'])
            tui.send(str(wrong + 1))
            tui.wait('Your answer — incorrect')
            tui.wait('Correct answer')
            tui.wait('Enter next question')
            assert 'Accuracy: 0.0%' in tui.screen.text
            if color == 'auto':
                assert set(tui.screen.colors_of('Your answer — incorrect')) == {(255, 100, 100)}
                assert set(tui.screen.colors_of('Correct answer')) == {(80, 216, 144)}
                tui.capture('choice-incorrect')
            else:
                assert set(tui.screen.colors_of('Your answer — incorrect')) == {None}
            tui.send('\n')
            tui.wait('Session summary')
            tui.send('\n')
            tui.wait('New progress profile')
            tui.send('q')
            assert tui.proc.wait(timeout=3) == 0
        finally:
            tui.close()

    # Bracketed paste preserves literal text rather than expanding input macros.
    source['id'] = 'paste-test'
    source['entries'] = [{'id': 'noel', 'type': 'other', 'de': {'prompt': 'Weihnachten', 'answers': [{'id': 'meaning', 'accept': ['Weihnachten']}]}, 'fr': [{'id': 'noel', 'forms': {'text': ['Noël']}}]}]
    paste_deck = tmp / 'paste.json'
    paste_deck.write_text(json.dumps(source, ensure_ascii=False))
    paste_profile = tmp / 'paste-progress.json'
    tui = Terminal(['--vocab', str(paste_deck), '--progress', str(paste_profile), '--mode', 'text', '--direction', 'de_fr', '--questions', '1'], tmp)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        tui.wait('Weihnachten')
        tui.send('\x1b[200~Noe:l\x1b[201~\n')
        tui.wait('Incorrect — review')
        tui.send('\n')
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    assert json.loads(paste_profile.read_text())['history'][0]['rows'][0]['text'] == 'Noe:l'

    # Practice stays on the question screen and repeats sweeps automatically.
    # An old saved question limit must no longer terminate a normal launch.
    endless_dir = tmp / 'endless'
    endless_dir.mkdir()
    (endless_dir / 'config.json').write_text(json.dumps({'session_questions': 1}))
    endless_profile = endless_dir / 'progress.json'
    tui = Terminal(['--vocab', str(sentence), '--progress', str(endless_profile),
                    '--mode', 'text', '--direction', 'de_fr'], endless_dir)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (')
        tui.send('\n')
        for question in range(1, 4):
            tui.wait('Insert')
            tui.wait('╰')
            prompt_row = next(i for i, line in enumerate(tui.screen.lines) if 'Ich' in line)
            tui.send('je ne me sens, pas bien!\n')
            tui.wait('Correct — well done')
            tui.wait('Next word…')
            assert 'Question ' not in tui.screen.text
            assert 'Ich' in tui.screen.lines[prompt_row]
            assert 'New vocabulary' not in tui.screen.text
        tui.wait('Insert')
        tui.send('\x1b')
        tui.wait('Normal')
        assert tui.screen.cursor_visible and tui.screen.cursor_shape == 2
        tui.send('q')
        tui.wait('Save and exit session')
        tui.send('jjj\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()
    events = json.loads(endless_profile.read_text())['history']
    assert len(events) == 3 and all(event['raw_correct'] for event in events)

    # Learn shows live threshold counts and selects only the requested hard word.
    menu_dir = tmp / 'sweep-menu'
    menu_dir.mkdir()
    menu_profile = menu_dir / 'progress.json'
    saved = json.loads(json.dumps(progress))
    pid = next(iter(saved['learning'][saved['session']['deck_id']]))
    learning = saved['learning'][saved['session']['deck_id']][pid]
    learning['completed_sweeps'] = 2
    for word in learning['words'].values():
        word['full_sweeps'] = 2
        word['score'] = 0.0
    learning['words']['cat']['score'] = 3.0
    saved['session']['finished'] = True
    menu_profile.write_text(json.dumps(saved))
    tui = Terminal(['--vocab', str(DEMO), '--progress', str(menu_profile),
                    '--mode', 'adaptive', '--direction', 'de_fr', '--questions', '1'], menu_dir)
    try:
        tui.wait('New progress profile')
        tui.send('jjjj\n')
        tui.wait('Default (Difficult sweep + Random)')
        tui.wait('Difficult sweep (1)')
        tui.wait('Very difficult sweep (1)')
        tui.capture('learn-difficult-menu')
        tui.send('jjj\n')
        tui.wait('Choose one answer')
        state = json.loads(menu_profile.read_text())['session']
        assert state['sweep']['mode'] == 'very_difficult'
        assert list(state['sweep']['items']) == ['cat']
        assert state['sweep']['items']['cat']['remaining'] == 3
        pending = state['pending']
        tui.send(str(pending['choices'].index(pending['entry']) + 1))
        tui.wait('Session summary')
        tui.send('\n')
        tui.wait('New progress profile')
        tui.send('q')
        assert tui.proc.wait(timeout=3) == 0
    finally:
        tui.close()

    # Partial nested settings retain defaults; malformed dates are rejected.
    custom_config = tmp / 'custom.json'
    custom_config.write_text(json.dumps({'theme': {'accent': 5}, 'keybindings': {'normal': {'H': '0'}}}))
    settings = json.loads(run('--config', custom_config, '--dump-config').stdout)
    assert settings['theme'] == {'accent': 5, 'error': '#ff6464', 'success': '#50d890'}
    run('--test-date', '2026-02-31', code=2)
    assert 'new_entries_per_day' not in settings
    assert 'retry_after_seconds' not in settings
    assert settings['retry_gap_min'] == 3 and settings['retry_gap_max'] == 10

    # The skill's complete example is accepted by the actual application.
    import re
    skill_text = (ROOT / 'skills/fvoci-vocab/references/format.md').read_text()
    skill_example = json.loads(re.findall(r'```json\n(.*?)\n```', skill_text, re.S)[0])
    example_file = tmp / 'skill-example.json'
    example_file.write_text(json.dumps(skill_example, ensure_ascii=False))
    run('--validate', example_file)

    # CLI installs are tested in a staging root, without changing the host.
    stage = tmp / 'stage'
    subprocess.run(['make', 'install', f'DESTDIR={stage}', 'PREFIX=/usr/local'], cwd=ROOT, check=True, capture_output=True)
    installed = stage / 'usr/local/bin/fvoci'
    subprocess.run([str(installed), '--validate', str(stage / 'usr/local/share/fvoci/demo.json')], check=True, capture_output=True)
    subprocess.run(['make', 'install-skill', f'DESTDIR={stage}', 'SKILLDIR=/skills'], cwd=ROOT, check=True, capture_output=True)
    assert (stage / 'skills/fvoci-vocab/SKILL.md').is_file()
    assert (stage / 'skills/fvoci-vocab/references/vocabulary.schema.json').is_file()
print('Passed CLI, terminal editing/macros/resize, sentence override, and staged installation checks.')
