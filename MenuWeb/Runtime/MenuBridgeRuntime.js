const MenuBridge = Object.freeze({
  requestTimeout: 5000,
  exitTimeout: 20000,
  wait(promise, milliseconds, description) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(description)), Math.max(1, milliseconds));
      // Late replies settle only this abandoned wait, never its caller's state.
      Promise.resolve(promise).then(
        value => { clearTimeout(timer); resolve(value); },
        error => { clearTimeout(timer); reject(error); }
      );
    });
  }
});
