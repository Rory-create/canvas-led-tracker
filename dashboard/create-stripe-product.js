// One-time setup script — run this once to create the Due Light product and price in Stripe.
// Usage:
//   STRIPE_SECRET_KEY=sk_live_... PRICE_CENTS=4900 node create-stripe-product.js
//
// PRICE_CENTS defaults to 4900 ($49.00). Change it to whatever price you decide on.
// After running, copy the printed STRIPE_PRICE_ID into your .env file.

require('dotenv').config();
const Stripe = require('stripe');

const secretKey = process.env.STRIPE_SECRET_KEY;
if (!secretKey) {
  console.error('Error: STRIPE_SECRET_KEY is not set.');
  process.exit(1);
}

const priceCents = parseInt(process.env.PRICE_CENTS || '4900', 10);
const stripe = Stripe(secretKey);

async function main() {
  console.log(`Creating Due Light product at $${(priceCents / 100).toFixed(2)}...`);

  const product = await stripe.products.create({
    name: 'Due Light',
    description: 'LED assignment deadline tracker. Works with any school that uses Canvas. One-time purchase, no subscription.',
    default_price_data: {
      currency: 'usd',
      unit_amount: priceCents,
    },
    shippable: true,
  });

  console.log('\nDone!\n');
  console.log('Product ID:', product.id);
  console.log('Price ID: ', product.default_price);
  console.log('\nAdd this to your .env file:');
  console.log(`STRIPE_PRICE_ID=${product.default_price}`);
}

main().catch((err) => {
  console.error('Stripe error:', err.message);
  process.exit(1);
});
