// One-time setup script — run this once to create the Due Light product and price in Stripe.
// Usage:
//   STRIPE_SECRET_KEY=sk_live_... PRICE_CENTS=4900 node create-stripe-product.js
//
// PRICE_CENTS is required — set it to your price in cents (e.g. 5000 for $50.00).
// After running, copy the printed STRIPE_PRICE_ID into your .env file.

require('dotenv').config();
const Stripe = require('stripe');

const secretKey = process.env.STRIPE_SECRET_KEY;
if (!secretKey) {
  console.error('Error: STRIPE_SECRET_KEY is not set.');
  process.exit(1);
}

const priceCents = parseInt(process.env.PRICE_CENTS || '', 10);
if (!priceCents || isNaN(priceCents)) {
  console.error('Error: PRICE_CENTS is required. Example: PRICE_CENTS=5000 node create-stripe-product.js');
  process.exit(1);
}
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
