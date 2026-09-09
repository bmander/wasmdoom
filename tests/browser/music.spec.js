import { test, expect } from '@playwright/test';

// Music is synthesised by an AudioWorkletNode. Fanning its output into an
// AnalyserNode, alongside its real connection, measures what it plays.
function tapWorkletOutput() {
  window.__peak = 0;
  const Original = window.AudioWorkletNode;
  window.AudioWorkletNode = class extends Original {
    constructor(context, name, options) {
      super(context, name, options);
      const analyser = context.createAnalyser();
      analyser.fftSize = 2048;
      this.connect(analyser);
      const samples = new Float32Array(analyser.fftSize);
      setInterval(() => {
        analyser.getFloatTimeDomainData(samples);
        for (const sample of samples) window.__peak = Math.max(window.__peak, Math.abs(sample));
      }, 20);
    }
  };
}

test('OPL music plays on the title screen and follows mute and pause', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.text().includes('Music unavailable')) errors.push(message.text()); });
  await page.addInitScript(tapWorkletOutput);
  await page.goto('/');
  await page.getByRole('button', {name: 'PLAY DOOM'}).click();
  await expect(page.locator('#status')).toHaveText('RUNNING', {timeout: 30000});

  // Measure the title screen, where DOOM plays music but no sound effects.
  const peak = async () => {
    await page.waitForTimeout(300); // Scheduled writes and the mute reach the audio thread.
    await page.evaluate(() => { window.__peak = 0; });
    await page.waitForTimeout(600);
    return page.evaluate(() => window.__peak);
  };

  expect(await peak()).toBeGreaterThan(0.001);
  await page.getByRole('button', {name: 'Sound on'}).click();
  expect(await peak()).toBe(0);
  await page.getByRole('button', {name: 'Sound off'}).click();
  expect(await peak()).toBeGreaterThan(0.001);
  await page.getByRole('button', {name: 'Pause', exact: false}).click();
  expect(await peak()).toBe(0);
  expect(errors).toEqual([]);
});
