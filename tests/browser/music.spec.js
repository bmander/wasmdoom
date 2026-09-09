import { test, expect } from '@playwright/test';

// The engine reaches the speakers through a single SDL ScriptProcessorNode.
// Wrapping its handler measures what the page actually plays, which an
// AnalyserNode inserted before the destination does not reliably report.
function tapAudioOutput() {
  window.__peak = 0;
  const property = Object.getOwnPropertyDescriptor(
    ScriptProcessorNode.prototype, 'onaudioprocess');
  Object.defineProperty(ScriptProcessorNode.prototype, 'onaudioprocess', {
    configurable: true,
    get() { return property.get.call(this); },
    set(handler) {
      property.set.call(this, function (event) {
        const result = handler.call(this, event);
        const samples = event.outputBuffer.getChannelData(0);
        let peak = 0;
        for (const sample of samples) peak = Math.max(peak, Math.abs(sample));
        window.__peak = Math.max(window.__peak, peak);
        return result;
      });
    },
  });
}

test('OPL music plays on the title screen and follows mute and pause', async ({page}) => {
  await page.addInitScript(tapAudioOutput);
  await page.goto('/');
  await page.getByRole('button', {name: 'PLAY DOOM'}).click();
  await expect(page.locator('#status')).toHaveText('RUNNING', {timeout: 30000});

  // Measure the title screen, where DOOM plays music but no sound effects.
  const peak = async () => {
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
});
