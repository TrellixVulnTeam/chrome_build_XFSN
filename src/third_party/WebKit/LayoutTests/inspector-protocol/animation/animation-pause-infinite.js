(async function(testRunner) {
  let {page, session, dp} = await testRunner.startHTML(`
    <div id='node' style='background-color: red; height: 100px'></div>
  `, 'Tests that the animation is correctly paused.');

  dp.Animation.enable();
  session.evaluate(`
    window.animation = node.animate([{ width: '100px' }, { width: '2000px' }], { duration: 10000, iterations: Infinity });
  `);

  var response = await dp.Animation.onceAnimationStarted();
  testRunner.log('Animation started');
  await dp.Animation.setPaused({ animations: [ response.params.animation.id ], paused: true });

  var nodeWidth = await session.evaluate('node.offsetWidth');
  var rafWidth = await session.evaluateAsync(`
    (function rafWidth() {
        var callback;
        var promise = new Promise((fulfill) => callback = fulfill);
        if (window.testRunner)
            testRunner.layoutAndPaintAsyncThen(() => callback(node.offsetWidth));
        return promise;
    })()
  `);
  testRunner.log(rafWidth === nodeWidth);
  testRunner.completeTest();
})
